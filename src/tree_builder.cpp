#include "topiary/impl/tree_builder.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <vector>

namespace topiary::internal {

namespace {

template <int Dim>
std::uint32_t build_recursive(std::vector<TreeNode>&  nodes,
                              LeafBucket&             buckets,
                              std::vector<BBox<Dim>>& leaf_bboxes,
                              const PointStore<Dim>&  store,
                              std::size_t             leaf_bucket_size,
                              std::uint32_t*          indices_begin,
                              std::uint32_t*          indices_end) {
    const auto count    = static_cast<std::size_t>(indices_end - indices_begin);
    const auto node_idx = static_cast<std::uint32_t>(nodes.size());
    nodes.emplace_back();

    if (count <= leaf_bucket_size) {
        const auto cap    = static_cast<std::uint16_t>(2 * leaf_bucket_size);
        const auto size   = static_cast<std::uint16_t>(count);
        const auto offset = buckets.allocate(cap);
        auto       slice  = buckets.view(offset, cap);
        BBox<Dim>  leaf_bbox{};
        leaf_bbox.min_corner.setConstant(std::numeric_limits<float>::infinity());
        leaf_bbox.max_corner.setConstant(-std::numeric_limits<float>::infinity());
        for (std::size_t i = 0; i < count; ++i) {
            const auto idx       = indices_begin[i];
            slice[i]             = BucketEntry{idx, store.generation(idx)};
            const auto& point    = store.point(idx);
            leaf_bbox.min_corner = leaf_bbox.min_corner.cwiseMin(point);
            leaf_bbox.max_corner = leaf_bbox.max_corner.cwiseMax(point);
        }
        const auto bbox_idx = static_cast<std::uint32_t>(leaf_bboxes.size());
        leaf_bboxes.push_back(leaf_bbox);

        TreeNode& leaf           = nodes[node_idx];
        leaf.is_leaf             = true;
        leaf.subtree_live_count  = static_cast<std::uint32_t>(count);
        leaf.subtree_total_count = static_cast<std::uint32_t>(count);
        leaf.bucket_offset       = offset;
        leaf.bucket_size         = size;
        leaf.bucket_capacity     = cap;
        leaf.leaf_bbox_idx       = bbox_idx;
        return node_idx;
    }

    // Median-of-max-spread: pick the axis with the widest extent over this subset.
    std::array<float, Dim> mins;
    std::array<float, Dim> maxs;
    for (int dim = 0; dim < Dim; ++dim) {
        mins[dim] = std::numeric_limits<float>::infinity();
        maxs[dim] = -std::numeric_limits<float>::infinity();
    }
    for (auto* it = indices_begin; it != indices_end; ++it) {
        const auto& point = store.point(*it);
        for (int dim = 0; dim < Dim; ++dim) {
            mins[dim] = std::min(mins[dim], point[dim]);
            maxs[dim] = std::max(maxs[dim], point[dim]);
        }
    }
    int   split_dim    = 0;
    float widest_range = maxs[0] - mins[0];
    for (int dim = 1; dim < Dim; ++dim) {
        const float range = maxs[dim] - mins[dim];
        if (range > widest_range) {
            widest_range = range;
            split_dim    = dim;
        }
    }

    auto* mid = indices_begin + count / 2;
    std::nth_element(
        indices_begin, mid, indices_end, [&store, split_dim](std::uint32_t lhs, std::uint32_t rhs) {
            return store.point(lhs)[split_dim] < store.point(rhs)[split_dim];
        });
    const float split_value = store.point(*mid)[split_dim];

    const std::uint32_t left =
        build_recursive<Dim>(nodes, buckets, leaf_bboxes, store, leaf_bucket_size, indices_begin, mid);
    const std::uint32_t right =
        build_recursive<Dim>(nodes, buckets, leaf_bboxes, store, leaf_bucket_size, mid, indices_end);

    TreeNode& node           = nodes[node_idx];
    node.is_leaf             = false;
    node.split_dim           = static_cast<std::uint8_t>(split_dim);
    node.split_value         = split_value;
    node.left                = left;
    node.right               = right;
    node.subtree_live_count  = nodes[left].subtree_live_count + nodes[right].subtree_live_count;
    node.subtree_total_count = nodes[left].subtree_total_count + nodes[right].subtree_total_count;
    return node_idx;
}

template <int Dim>
void collect_live_indices_in_subtree(const std::vector<TreeNode>& nodes,
                                     const LeafBucket&            buckets,
                                     const PointStore<Dim>&       store,
                                     std::uint32_t                node_idx,
                                     std::vector<std::uint32_t>&  out) {
    const TreeNode& node = nodes[node_idx];
    if (node.is_leaf) {
        for (const auto& entry : buckets.view(node.bucket_offset, node.bucket_size)) {
            if (store.generation(entry.index) != entry.gen || !store.is_live(entry.index)) {
                continue;
            }
            out.push_back(entry.index);
        }
        return;
    }
    collect_live_indices_in_subtree<Dim>(nodes, buckets, store, node.left,  out);
    collect_live_indices_in_subtree<Dim>(nodes, buckets, store, node.right, out);
}

/// Collect live indices under `scapegoat`, build a fresh subtree, and overwrite the scapegoat
/// node-pool slot so the parent's child pointer remains valid without reparenting.
template <int Dim>
void rebuild_subtree_in_place(std::vector<TreeNode>&  nodes,
                              LeafBucket&             buckets,
                              std::vector<BBox<Dim>>& leaf_bboxes,
                              const PointStore<Dim>&  store,
                              std::size_t             leaf_bucket_size,
                              std::uint32_t           scapegoat) {
    std::vector<std::uint32_t> live_indices;
    live_indices.reserve(nodes[scapegoat].subtree_live_count);
    collect_live_indices_in_subtree<Dim>(nodes, buckets, store, scapegoat, live_indices);

    const auto new_subroot_idx = build_recursive<Dim>(nodes,
                                                      buckets,
                                                      leaf_bboxes,
                                                      store,
                                                      leaf_bucket_size,
                                                      live_indices.data(),
                                                      live_indices.data() + live_indices.size());
    nodes[scapegoat] = nodes[new_subroot_idx];
}

/// Recursive top-down sweep: rebuild violators in place; for non-violators recurse into both
/// children then re-check self once because a child rebuild may have shifted the ratio.
template <int Dim>
void sweep(std::vector<TreeNode>&  nodes,
           LeafBucket&             buckets,
           std::vector<BBox<Dim>>& leaf_bboxes,
           const PointStore<Dim>&  store,
           std::size_t             leaf_bucket_size,
           float                   alpha,
           float                   tombstone_threshold,
           std::uint32_t           node_idx) {
    if (node_idx == TreeNode::INVALID) return;

    if (nodes[node_idx].is_leaf) {
        if (nodes[node_idx].bucket_size > 2 * leaf_bucket_size) {
            rebuild_subtree_in_place<Dim>(
                nodes, buckets, leaf_bboxes, store, leaf_bucket_size, node_idx);
        }
        return;
    }

    auto is_violator = [&](std::uint32_t idx) -> bool {
        const TreeNode& n = nodes[idx];
        if (n.is_leaf) return false;
        const auto total = n.subtree_total_count;
        if (total == 0) return false;
        const auto left_total  = nodes[n.left].subtree_total_count;
        const auto right_total = nodes[n.right].subtree_total_count;
        const auto max_child   = std::max(left_total, right_total);
        const bool unbalanced =
            static_cast<float>(max_child) > alpha * static_cast<float>(total);
        const bool tombstoned =
            (total > n.subtree_live_count)
            && (static_cast<float>(total - n.subtree_live_count) / static_cast<float>(total)
                >= tombstone_threshold);
        return unbalanced || tombstoned;
    };

    if (is_violator(node_idx)) {
        rebuild_subtree_in_place<Dim>(
            nodes, buckets, leaf_bboxes, store, leaf_bucket_size, node_idx);
        return;
    }

    const auto left  = nodes[node_idx].left;
    const auto right = nodes[node_idx].right;
    sweep<Dim>(
        nodes, buckets, leaf_bboxes, store, leaf_bucket_size, alpha, tombstone_threshold, left);
    sweep<Dim>(
        nodes, buckets, leaf_bboxes, store, leaf_bucket_size, alpha, tombstone_threshold, right);

    // Post-children re-check: a child rebuild may have shifted this node's left/right ratio
    // or its own live/total ratio. At most one rebuild here; do not recurse again.
    if (is_violator(node_idx)) {
        rebuild_subtree_in_place<Dim>(
            nodes, buckets, leaf_bboxes, store, leaf_bucket_size, node_idx);
    }
}

} // namespace

template <int Dim>
TreeBuilder<Dim>::TreeBuilder(std::vector<TreeNode>&   node_pool,
                              LeafBucket&              leaf_buckets,
                              std::vector<BBox<Dim>>&  leaf_bboxes,
                              BBox<Dim>&               root_bbox,
                              const PointStore<Dim>&   points,
                              std::size_t              leaf_bucket_size,
                              float                    alpha,
                              float                    tombstone_threshold)
    : node_pool_(node_pool)
    , leaf_buckets_(leaf_buckets)
    , leaf_bboxes_(leaf_bboxes)
    , root_bbox_(root_bbox)
    , points_(points)
    , leaf_bucket_size_(leaf_bucket_size)
    , alpha_(alpha)
    , tombstone_threshold_(tombstone_threshold) {}

template <int Dim>
std::uint32_t TreeBuilder<Dim>::rebuild(std::span<std::uint32_t> live_indices) {
    if (live_indices.empty()) {
        return TreeNode::INVALID;
    }
    root_bbox_.min_corner.setConstant(std::numeric_limits<float>::infinity());
    root_bbox_.max_corner.setConstant(-std::numeric_limits<float>::infinity());
    for (auto idx : live_indices) {
        const auto& point    = points_.point(idx);
        root_bbox_.min_corner = root_bbox_.min_corner.cwiseMin(point);
        root_bbox_.max_corner = root_bbox_.max_corner.cwiseMax(point);
    }
    return build_recursive<Dim>(node_pool_,
                                leaf_buckets_,
                                leaf_bboxes_,
                                points_,
                                leaf_bucket_size_,
                                live_indices.data(),
                                live_indices.data() + live_indices.size());
}

template <int Dim>
void TreeBuilder<Dim>::maybe_partial_rebuild(std::uint32_t current_root) {
    if (current_root == TreeNode::INVALID) return;
    sweep<Dim>(node_pool_,
               leaf_buckets_,
               leaf_bboxes_,
               points_,
               leaf_bucket_size_,
               alpha_,
               tombstone_threshold_,
               current_root);
}


template <int Dim>
void TreeBuilder<Dim>::insert_index(std::uint32_t root, std::uint32_t index) {
    const auto& point = points_.point(index);
    root_bbox_.min_corner = root_bbox_.min_corner.cwiseMin(point);
    root_bbox_.max_corner = root_bbox_.max_corner.cwiseMax(point);

    std::uint32_t node_idx = root;
    while (true) {
        TreeNode& node = node_pool_[node_idx];
        ++node.subtree_total_count;
        ++node.subtree_live_count;
        if (node.is_leaf) {
            const BucketEntry entry{index, points_.generation(index)};
            const bool        ok = leaf_buckets_.push(node.bucket_offset,
                                               node.bucket_size,
                                               node.bucket_capacity,
                                               entry);
            assert(ok && "LeafBucket::push failed despite eager split");
            (void)ok;
            auto& bbox      = leaf_bboxes_[node.leaf_bbox_idx];
            bbox.min_corner = bbox.min_corner.cwiseMin(point);
            bbox.max_corner = bbox.max_corner.cwiseMax(point);
            // Proactive split: a leaf that just reached its 2B cap is split now so a
            // same-batch insert routing here lands in one of the new children.
            if (node.bucket_size == node.bucket_capacity) {
                rebuild_subtree_in_place<Dim>(node_pool_,
                                              leaf_buckets_,
                                              leaf_bboxes_,
                                              points_,
                                              leaf_bucket_size_,
                                              node_idx);
            }
            return;
        }
        node_idx = (point[node.split_dim] < node.split_value) ? node.left : node.right;
    }
}

template <int Dim>
void TreeBuilder<Dim>::tombstone_index(std::uint32_t root, std::uint32_t index) {
    const auto&   point    = points_.point(index);
    std::uint32_t node_idx = root;
    while (true) {
        TreeNode& node = node_pool_[node_idx];
        --node.subtree_live_count;
        if (node.is_leaf) {
            return;
        }
        node_idx = (point[node.split_dim] < node.split_value) ? node.left : node.right;
    }
}

template class TreeBuilder<2>;
template class TreeBuilder<3>;
template class TreeBuilder<4>;

} // namespace topiary::internal
