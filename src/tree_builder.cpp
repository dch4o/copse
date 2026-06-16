#include "copse/impl/tree_builder.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace copse::internal {

static_assert(sizeof(TreeNode) == 32, "TreeNode must stay 32 B for cache density on descent");

namespace {

template <int Dim>
std::uint32_t build_recursive(std::vector<TreeNode>&  nodes,
                              LeafBucket&             leaf_buckets,
                              std::vector<BBox<Dim>>& leaf_bboxes,
                              const PointStore<Dim>&  points,
                              std::size_t             leaf_bucket_size,
                              std::uint32_t*          indices_begin,
                              std::uint32_t*          indices_end) {
    const auto count    = static_cast<std::size_t>(indices_end - indices_begin);
    const auto node_idx = static_cast<std::uint32_t>(nodes.size());
    nodes.emplace_back();

    if (count <= leaf_bucket_size) {
        const auto cap    = static_cast<std::uint16_t>(2 * leaf_bucket_size);
        const auto size   = static_cast<std::uint16_t>(count);
        const auto offset = leaf_buckets.allocate(cap);
        auto       slice  = leaf_buckets.view(offset, cap);
        BBox<Dim>  leaf_bbox{};
        leaf_bbox.min_corner.setConstant(std::numeric_limits<float>::infinity());
        leaf_bbox.max_corner.setConstant(-std::numeric_limits<float>::infinity());
        for (std::size_t i = 0; i < count; ++i) {
            const auto idx       = indices_begin[i];
            slice[i]             = BucketEntry{idx, points.generation(idx)};
            const auto& point    = points.point(idx);
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
        const auto& point = points.point(*it);
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
        indices_begin, mid, indices_end, [&points, split_dim](std::uint32_t lhs, std::uint32_t rhs) {
            return points.point(lhs)[split_dim] < points.point(rhs)[split_dim];
        });
    const float split_value = points.point(*mid)[split_dim];

    const std::uint32_t left =
        build_recursive<Dim>(nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, indices_begin, mid);
    const std::uint32_t right =
        build_recursive<Dim>(nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, mid, indices_end);

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
                                     const LeafBucket&            leaf_buckets,
                                     const PointStore<Dim>&       points,
                                     std::uint32_t                node_idx,
                                     std::vector<std::uint32_t>&  out) {
    const TreeNode& node = nodes[node_idx];
    if (node.is_leaf) {
        for (const auto& entry : leaf_buckets.view(node.bucket_offset, node.bucket_size)) {
            if (points.generation(entry.index) != entry.gen || !points.is_live(entry.index)) {
                continue;
            }
            out.push_back(entry.index);
        }
        return;
    }
    collect_live_indices_in_subtree<Dim>(nodes, leaf_buckets, points, node.left, out);
    collect_live_indices_in_subtree<Dim>(nodes, leaf_buckets, points, node.right, out);
}

template <int Dim>
void rebuild_subtree_in_place(std::vector<TreeNode>&  nodes,
                              LeafBucket&             leaf_buckets,
                              std::vector<BBox<Dim>>& leaf_bboxes,
                              const PointStore<Dim>&  points,
                              std::size_t             leaf_bucket_size,
                              std::uint32_t           scapegoat) {
    std::vector<std::uint32_t> live_indices;
    live_indices.reserve(nodes[scapegoat].subtree_live_count);
    collect_live_indices_in_subtree<Dim>(nodes, leaf_buckets, points, scapegoat, live_indices);

    const auto new_subroot_idx = build_recursive<Dim>(nodes,
                                                      leaf_buckets,
                                                      leaf_bboxes,
                                                      points,
                                                      leaf_bucket_size,
                                                      live_indices.data(),
                                                      live_indices.data() + live_indices.size());
    nodes[scapegoat]           = nodes[new_subroot_idx];
}

template <int Dim>
bool is_violator(const std::vector<TreeNode>& nodes,
                 std::uint32_t                node_idx,
                 float                        alpha,
                 float                        tombstone_threshold) {
    const TreeNode& n = nodes[node_idx];
    if (n.is_leaf)
        return false;
    const auto total = n.subtree_total_count;
    if (total == 0)
        return false;
    const auto left_total  = nodes[n.left].subtree_total_count;
    const auto right_total = nodes[n.right].subtree_total_count;
    const auto max_child   = std::max(left_total, right_total);
    const bool unbalanced  = static_cast<float>(max_child) > alpha * static_cast<float>(total);
    const bool tombstoned =
        (total > n.subtree_live_count) &&
        (static_cast<float>(total - n.subtree_live_count) / static_cast<float>(total) >= tombstone_threshold);
    return unbalanced || tombstoned;
}

template <int Dim>
void sweep_full(std::vector<TreeNode>&  nodes,
                LeafBucket&             leaf_buckets,
                std::vector<BBox<Dim>>& leaf_bboxes,
                const PointStore<Dim>&  points,
                std::size_t             leaf_bucket_size,
                float                   alpha,
                float                   tombstone_threshold,
                std::uint32_t           node_idx) {
    if (node_idx == TreeNode::INVALID)
        return;

    if (nodes[node_idx].is_leaf) {
        if (nodes[node_idx].bucket_size > 2 * leaf_bucket_size) {
            rebuild_subtree_in_place<Dim>(
                nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, node_idx);
        }
        return;
    }

    if (is_violator<Dim>(nodes, node_idx, alpha, tombstone_threshold)) {
        rebuild_subtree_in_place<Dim>(nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, node_idx);
        return;
    }

    const auto left  = nodes[node_idx].left;
    const auto right = nodes[node_idx].right;
    sweep_full<Dim>(
        nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, alpha, tombstone_threshold, left);
    sweep_full<Dim>(
        nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, alpha, tombstone_threshold, right);

    if (is_violator<Dim>(nodes, node_idx, alpha, tombstone_threshold)) {
        rebuild_subtree_in_place<Dim>(nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, node_idx);
    }
}

template <int Dim>
void sweep_scoped(std::vector<TreeNode>&            nodes,
                  LeafBucket&                       leaf_buckets,
                  std::vector<BBox<Dim>>&           leaf_bboxes,
                  const PointStore<Dim>&            points,
                  std::size_t                       leaf_bucket_size,
                  float                             alpha,
                  float                             tombstone_threshold,
                  const std::vector<std::uint32_t>& dirty,
                  std::uint32_t                     node_idx) {
    if (node_idx == TreeNode::INVALID)
        return;

    if (nodes[node_idx].is_leaf) {
        if (nodes[node_idx].bucket_size > 2 * leaf_bucket_size) {
            rebuild_subtree_in_place<Dim>(
                nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, node_idx);
        }
        return;
    }

    if (is_violator<Dim>(nodes, node_idx, alpha, tombstone_threshold)) {
        rebuild_subtree_in_place<Dim>(nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, node_idx);
        return;
    }

    const auto left  = nodes[node_idx].left;
    const auto right = nodes[node_idx].right;
    if (std::binary_search(dirty.begin(), dirty.end(), left)) {
        sweep_scoped<Dim>(nodes,
                          leaf_buckets,
                          leaf_bboxes,
                          points,
                          leaf_bucket_size,
                          alpha,
                          tombstone_threshold,
                          dirty,
                          left);
    }
    if (std::binary_search(dirty.begin(), dirty.end(), right)) {
        sweep_scoped<Dim>(nodes,
                          leaf_buckets,
                          leaf_bboxes,
                          points,
                          leaf_bucket_size,
                          alpha,
                          tombstone_threshold,
                          dirty,
                          right);
    }

    if (is_violator<Dim>(nodes, node_idx, alpha, tombstone_threshold)) {
        rebuild_subtree_in_place<Dim>(nodes, leaf_buckets, leaf_bboxes, points, leaf_bucket_size, node_idx);
    }
}

template <int Dim>
std::size_t release_subtree(std::vector<TreeNode>& nodes,
                            const LeafBucket&      leaf_buckets,
                            PointStore<Dim>&       points,
                            std::uint32_t          node_idx) {
    TreeNode&   node    = nodes[node_idx];
    std::size_t cleared = 0;
    if (node.is_leaf) {
        for (const auto& entry : leaf_buckets.view(node.bucket_offset, node.bucket_size)) {
            if (points.generation(entry.index) != entry.gen || !points.is_live(entry.index)) {
                continue;
            }
            points.release(entry.index);
            ++cleared;
        }
    } else {
        cleared += release_subtree<Dim>(nodes, leaf_buckets, points, node.left);
        cleared += release_subtree<Dim>(nodes, leaf_buckets, points, node.right);
    }
    node.subtree_live_count -= static_cast<std::uint32_t>(cleared);
    return cleared;
}

template <int Dim>
std::size_t delete_box_leaf(const TreeNode&               node,
                               const LeafBucket&             leaf_buckets,
                               PointStore<Dim>&              points,
                               const detail::PointType<Dim>& min_corner,
                               const detail::PointType<Dim>& max_corner) {
    std::size_t cleared = 0;
    for (const auto& entry : leaf_buckets.view(node.bucket_offset, node.bucket_size)) {
        if (points.generation(entry.index) != entry.gen || !points.is_live(entry.index)) {
            continue;
        }
        const auto& point = points.point(entry.index);
        if ((point.array() >= min_corner.array()).all() && (point.array() <= max_corner.array()).all()) {
            points.release(entry.index);
            ++cleared;
        }
    }
    return cleared;
}

template <int Dim>
bool box_disjoint(const BBox<Dim>&              partition,
                  const detail::PointType<Dim>& min_corner,
                  const detail::PointType<Dim>& max_corner) {
    return (partition.max_corner.array() < min_corner.array()).any() ||
           (partition.min_corner.array() > max_corner.array()).any();
}

template <int Dim>
bool box_contained(const BBox<Dim>&              partition,
                   const detail::PointType<Dim>& min_corner,
                   const detail::PointType<Dim>& max_corner) {
    return (partition.min_corner.array() >= min_corner.array()).all() &&
           (partition.max_corner.array() <= max_corner.array()).all();
}

template <int Dim>
std::size_t delete_box_recurse(std::vector<TreeNode>&        nodes,
                                  const LeafBucket&             leaf_buckets,
                                  const std::vector<BBox<Dim>>& leaf_bboxes,
                                  PointStore<Dim>&              points,
                                  std::uint32_t                 node_idx,
                                  BBox<Dim>                     partition,
                                  const detail::PointType<Dim>& min_corner,
                                  const detail::PointType<Dim>& max_corner,
                                  std::vector<std::uint32_t>&   modified) {
    if (box_disjoint<Dim>(partition, min_corner, max_corner)) {
        return 0;
    }
    if (box_contained<Dim>(partition, min_corner, max_corner)) {
        const std::size_t cleared = release_subtree<Dim>(nodes, leaf_buckets, points, node_idx);
        if (cleared != 0) {
            modified.push_back(node_idx); // counts dropped to 0 → trigger must re-check this subtree
        }
        return cleared;
    }

    TreeNode&   node    = nodes[node_idx];
    std::size_t cleared = 0;
    if (node.is_leaf) {
        // Tight leaf BBox refines the loose partition box before per-point checks. Leaves are not
        // violator-checked by deletes (no bucket growth), so ancestors carry the trigger record.
        const BBox<Dim>& leaf_bbox = leaf_bboxes[node.leaf_bbox_idx];
        if (box_disjoint<Dim>(leaf_bbox, min_corner, max_corner)) {
            return 0;
        }
        if (box_contained<Dim>(leaf_bbox, min_corner, max_corner)) {
            cleared = release_subtree<Dim>(nodes, leaf_buckets, points, node_idx);
            return cleared;
        }
        cleared = delete_box_leaf<Dim>(node, leaf_buckets, points, min_corner, max_corner);
        node.subtree_live_count -= static_cast<std::uint32_t>(cleared);
        return cleared;
    }

    const auto  dim   = node.split_dim;
    const float split = node.split_value;
    const auto  left  = node.left;
    const auto  right = node.right;

    BBox<Dim> left_box       = partition;
    left_box.max_corner[dim] = std::min(left_box.max_corner[dim], split);
    cleared += delete_box_recurse<Dim>(
        nodes, leaf_buckets, leaf_bboxes, points, left, left_box, min_corner, max_corner, modified);

    BBox<Dim> right_box       = partition;
    right_box.min_corner[dim] = std::max(right_box.min_corner[dim], split);
    cleared += delete_box_recurse<Dim>(
        nodes, leaf_buckets, leaf_bboxes, points, right, right_box, min_corner, max_corner, modified);

    nodes[node_idx].subtree_live_count -= static_cast<std::uint32_t>(cleared);
    if (cleared != 0) {
        modified.push_back(node_idx);
    }
    return cleared;
}

template <int Dim>
float box_nearest_sq_dist(const BBox<Dim>& partition, const detail::PointType<Dim>& center) {
    const auto below = (partition.min_corner.array() - center.array()).max(0.0f);
    const auto above = (center.array() - partition.max_corner.array()).max(0.0f);
    return (below.square() + above.square()).sum();
}

template <int Dim>
float box_farthest_sq_dist(const BBox<Dim>& partition, const detail::PointType<Dim>& center) {
    const auto to_min = (center.array() - partition.min_corner.array()).abs();
    const auto to_max = (center.array() - partition.max_corner.array()).abs();
    return to_min.max(to_max).square().sum();
}

template <int Dim>
std::size_t delete_outside_radius_leaf(const TreeNode&               node,
                                       const LeafBucket&             leaf_buckets,
                                       PointStore<Dim>&              points,
                                       const detail::PointType<Dim>& center,
                                       float                         sq_radius) {
    std::size_t cleared = 0;
    for (const auto& entry : leaf_buckets.view(node.bucket_offset, node.bucket_size)) {
        if (points.generation(entry.index) != entry.gen || !points.is_live(entry.index)) {
            continue;
        }
        if ((points.point(entry.index) - center).squaredNorm() > sq_radius) {
            points.release(entry.index);
            ++cleared;
        }
    }
    return cleared;
}

template <int Dim>
std::size_t delete_outside_radius_recurse(std::vector<TreeNode>&        nodes,
                                          const LeafBucket&             leaf_buckets,
                                          const std::vector<BBox<Dim>>& leaf_bboxes,
                                          PointStore<Dim>&              points,
                                          std::uint32_t                 node_idx,
                                          BBox<Dim>                     partition,
                                          const detail::PointType<Dim>& center,
                                          float                         sq_radius,
                                          std::vector<std::uint32_t>&   modified) {
    // Partition fully inside the sphere: every point is within r, keep all.
    if (box_farthest_sq_dist<Dim>(partition, center) <= sq_radius) {
        return 0;
    }
    // Partition fully outside the sphere: every point is strictly outside, release all.
    if (box_nearest_sq_dist<Dim>(partition, center) > sq_radius) {
        const std::size_t cleared = release_subtree<Dim>(nodes, leaf_buckets, points, node_idx);
        if (cleared != 0) {
            modified.push_back(node_idx);
        }
        return cleared;
    }

    TreeNode&   node    = nodes[node_idx];
    std::size_t cleared = 0;
    if (node.is_leaf) {
        const BBox<Dim>& leaf_bbox = leaf_bboxes[node.leaf_bbox_idx];
        if (box_farthest_sq_dist<Dim>(leaf_bbox, center) <= sq_radius) {
            return 0;
        }
        if (box_nearest_sq_dist<Dim>(leaf_bbox, center) > sq_radius) {
            return release_subtree<Dim>(nodes, leaf_buckets, points, node_idx);
        }
        cleared = delete_outside_radius_leaf<Dim>(node, leaf_buckets, points, center, sq_radius);
        node.subtree_live_count -= static_cast<std::uint32_t>(cleared);
        return cleared;
    }

    const auto  dim   = node.split_dim;
    const float split = node.split_value;
    const auto  left  = node.left;
    const auto  right = node.right;

    BBox<Dim> left_box       = partition;
    left_box.max_corner[dim] = std::min(left_box.max_corner[dim], split);
    cleared += delete_outside_radius_recurse<Dim>(
        nodes, leaf_buckets, leaf_bboxes, points, left, left_box, center, sq_radius, modified);

    BBox<Dim> right_box       = partition;
    right_box.min_corner[dim] = std::max(right_box.min_corner[dim], split);
    cleared += delete_outside_radius_recurse<Dim>(
        nodes, leaf_buckets, leaf_bboxes, points, right, right_box, center, sq_radius, modified);

    nodes[node_idx].subtree_live_count -= static_cast<std::uint32_t>(cleared);
    if (cleared != 0) {
        modified.push_back(node_idx);
    }
    return cleared;
}

} // namespace

template <int Dim>
TreeBuilder<Dim>::TreeBuilder(std::vector<TreeNode>&  nodes,
                              LeafBucket&             leaf_buckets,
                              std::vector<BBox<Dim>>& leaf_bboxes,
                              BBox<Dim>&              root_bbox,
                              PointStore<Dim>&        points,
                              std::size_t             leaf_bucket_size,
                              float                   alpha,
                              float                   tombstone_threshold)
    : nodes_(nodes)
    , leaf_buckets_(leaf_buckets)
    , leaf_bboxes_(leaf_bboxes)
    , root_bbox_(root_bbox)
    , points_(points)
    , leaf_bucket_size_(leaf_bucket_size)
    , alpha_(alpha)
    , tombstone_threshold_(tombstone_threshold) {}

template <int Dim>
std::uint32_t TreeBuilder<Dim>::rebuild(std::span<std::uint32_t> live_indices) {
    modified_nodes_.clear(); // node indices become stale after a from-scratch rebuild
    if (live_indices.empty()) {
        return TreeNode::INVALID;
    }
    root_bbox_.min_corner.setConstant(std::numeric_limits<float>::infinity());
    root_bbox_.max_corner.setConstant(-std::numeric_limits<float>::infinity());
    for (auto idx : live_indices) {
        const auto& point     = points_.point(idx);
        root_bbox_.min_corner = root_bbox_.min_corner.cwiseMin(point);
        root_bbox_.max_corner = root_bbox_.max_corner.cwiseMax(point);
    }
    return build_recursive<Dim>(nodes_,
                                leaf_buckets_,
                                leaf_bboxes_,
                                points_,
                                leaf_bucket_size_,
                                live_indices.data(),
                                live_indices.data() + live_indices.size());
}

template <int Dim>
void TreeBuilder<Dim>::maybe_partial_rebuild(std::uint32_t current_root) {
    if (current_root == TreeNode::INVALID || modified_nodes_.empty()) {
        modified_nodes_.clear();
        return;
    }
    // Dense mutations (large batch inserts) record a touched node per point-path, dwarfing the node
    // count; there a full sweep beats sort + scoped walk. Sparse deletes record a node per touched
    // subtree, so the scoped walk pays off. Route on the raw record count vs. the node pool size.
    if (modified_nodes_.size() >= nodes_.size()) {
        sweep_full<Dim>(nodes_,
                        leaf_buckets_,
                        leaf_bboxes_,
                        points_,
                        leaf_bucket_size_,
                        alpha_,
                        tombstone_threshold_,
                        current_root);
    } else {
        std::sort(modified_nodes_.begin(), modified_nodes_.end());
        modified_nodes_.erase(std::unique(modified_nodes_.begin(), modified_nodes_.end()),
                              modified_nodes_.end());
        sweep_scoped<Dim>(nodes_,
                          leaf_buckets_,
                          leaf_bboxes_,
                          points_,
                          leaf_bucket_size_,
                          alpha_,
                          tombstone_threshold_,
                          modified_nodes_,
                          current_root);
    }
    modified_nodes_.clear();
}

template <int Dim>
void TreeBuilder<Dim>::maybe_partial_rebuild_full(std::uint32_t current_root) {
    modified_nodes_.clear();
    if (current_root == TreeNode::INVALID) {
        return;
    }
    sweep_full<Dim>(nodes_,
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
    const auto& point     = points_.point(index);
    root_bbox_.min_corner = root_bbox_.min_corner.cwiseMin(point);
    root_bbox_.max_corner = root_bbox_.max_corner.cwiseMax(point);

    std::uint32_t node_idx = root;
    while (true) {
        TreeNode& node = nodes_[node_idx];
        ++node.subtree_total_count;
        ++node.subtree_live_count;
        if (node.is_leaf) {
            const BucketEntry entry{index, points_.generation(index)};
            const bool        ok =
                leaf_buckets_.push(node.bucket_offset, node.bucket_size, node.bucket_capacity, entry);
            assert(ok && "LeafBucket::push failed despite eager split");
            (void)ok;
            auto& bbox      = leaf_bboxes_[node.leaf_bbox_idx];
            bbox.min_corner = bbox.min_corner.cwiseMin(point);
            bbox.max_corner = bbox.max_corner.cwiseMax(point);
            // Proactive split: a leaf that just reached its 2B cap is split now so a
            // same-batch insert routing here lands in one of the new children.
            if (node.bucket_size == node.bucket_capacity) {
                rebuild_subtree_in_place<Dim>(
                    nodes_, leaf_buckets_, leaf_bboxes_, points_, leaf_bucket_size_, node_idx);
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
        TreeNode& node = nodes_[node_idx];
        --node.subtree_live_count;
        if (node.is_leaf) {
            return;
        }
        modified_nodes_.push_back(node_idx);
        node_idx = (point[node.split_dim] < node.split_value) ? node.left : node.right;
    }
}

template <int Dim>
std::size_t TreeBuilder<Dim>::delete_box(std::uint32_t root, const BBox<Dim>& box) {
    if (root == TreeNode::INVALID) {
        return 0;
    }
    return delete_box_recurse<Dim>(nodes_,
                                      leaf_buckets_,
                                      leaf_bboxes_,
                                      points_,
                                      root,
                                      root_bbox_,
                                      box.min_corner,
                                      box.max_corner,
                                      modified_nodes_);
}

template <int Dim>
std::size_t TreeBuilder<Dim>::delete_outside_radius(std::uint32_t root, const Point& center, float r) {
    if (root == TreeNode::INVALID) {
        return 0;
    }
    return delete_outside_radius_recurse<Dim>(
        nodes_, leaf_buckets_, leaf_bboxes_, points_, root, root_bbox_, center, r * r, modified_nodes_);
}

template class TreeBuilder<2>;
template class TreeBuilder<3>;
template class TreeBuilder<4>;

} // namespace copse::internal
