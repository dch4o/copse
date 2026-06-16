#include "topiary/impl/search_kernel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <vector>

namespace topiary::internal {

namespace {

struct HeapEntry {
    float         sq_dist;
    std::uint32_t index;
};

struct HeapCompare {
    bool operator()(const HeapEntry& lhs, const HeapEntry& rhs) const noexcept {
        return lhs.sq_dist < rhs.sq_dist;
    }
};

template <int Dim>
void scan_leaf(const TreeNode&                          node,
               const LeafBucket&                        leaf_buckets,
               const PointStore<Dim>&                   points,
               const typename SearchKernel<Dim>::Point& query,
               std::size_t                              k_max,
               std::vector<HeapEntry>&                  heap,
               float&                                   worst_sq_dist) {
    const auto slice = leaf_buckets.view(node.bucket_offset, node.bucket_size);
    for (const auto& entry : slice) {
        if (points.generation(entry.index) != entry.gen || !points.is_live(entry.index)) {
            continue;
        }
        const float sq_dist = (points.point(entry.index) - query).squaredNorm();
        if (sq_dist >= worst_sq_dist) {
            continue;
        }
        heap.push_back({sq_dist, entry.index});
        std::push_heap(heap.begin(), heap.end(), HeapCompare{});
        if (heap.size() > k_max) {
            std::pop_heap(heap.begin(), heap.end(), HeapCompare{});
            heap.pop_back();
            worst_sq_dist = heap.front().sq_dist;
        } else if (heap.size() == k_max) {
            worst_sq_dist = heap.front().sq_dist;
        }
    }
}

template <int Dim>
void descend(const std::vector<TreeNode>&             nodes,
             const LeafBucket&                        leaf_buckets,
             const PointStore<Dim>&                   points,
             std::uint32_t                            node_idx,
             const typename SearchKernel<Dim>::Point& query,
             std::size_t                              k_max,
             std::vector<HeapEntry>&                  heap,
             float&                                   worst_sq_dist,
             std::array<float, Dim>&                  min_sq_axis_dist,
             float                                    min_sq_dist) {
    if (node_idx == TreeNode::INVALID) {
        return;
    }
    const TreeNode& node = nodes[node_idx];
    if (node.is_leaf) {
        scan_leaf<Dim>(node, leaf_buckets, points, query, k_max, heap, worst_sq_dist);
        return;
    }
    const auto  dim        = node.split_dim;
    const float split      = node.split_value;
    const float diff       = query[dim] - split;
    const auto  near_child = (diff < 0.0f) ? node.left : node.right;
    const auto  far_child  = (diff < 0.0f) ? node.right : node.left;

    descend<Dim>(nodes,
                 leaf_buckets,
                 points,
                 near_child,
                 query,
                 k_max,
                 heap,
                 worst_sq_dist,
                 min_sq_axis_dist,
                 min_sq_dist);

    // Entering the far child crosses the split plane on `dim`: replace that
    // axis's contribution with diff^2 and re-test the box lower bound.
    const float far_min_sq_dist = min_sq_dist - min_sq_axis_dist[dim] + diff * diff;
    if (far_min_sq_dist < worst_sq_dist) {
        const float saved_axis_dist = min_sq_axis_dist[dim];
        min_sq_axis_dist[dim]       = diff * diff;
        descend<Dim>(nodes,
                     leaf_buckets,
                     points,
                     far_child,
                     query,
                     k_max,
                     heap,
                     worst_sq_dist,
                     min_sq_axis_dist,
                     far_min_sq_dist);
        min_sq_axis_dist[dim] = saved_axis_dist;
    }
}

template <int Dim>
void collect_leaf(const TreeNode&                          node,
                  const LeafBucket&                        leaf_buckets,
                  const PointStore<Dim>&                   points,
                  const typename SearchKernel<Dim>::Point& query,
                  float                                    sq_radius,
                  std::vector<std::uint32_t>&              matches) {
    const auto slice = leaf_buckets.view(node.bucket_offset, node.bucket_size);
    for (const auto& entry : slice) {
        if (points.generation(entry.index) != entry.gen || !points.is_live(entry.index)) {
            continue;
        }
        const float sq_dist = (points.point(entry.index) - query).squaredNorm();
        if (sq_dist < sq_radius) {
            matches.push_back(entry.index);
        }
    }
}

template <int Dim>
void collect_descend(const std::vector<TreeNode>&             nodes,
                     const LeafBucket&                        leaf_buckets,
                     const PointStore<Dim>&                   points,
                     std::uint32_t                            node_idx,
                     const typename SearchKernel<Dim>::Point& query,
                     float                                    sq_radius,
                     std::vector<std::uint32_t>&              matches) {
    if (node_idx == TreeNode::INVALID) {
        return;
    }
    const TreeNode& node = nodes[node_idx];
    if (node.is_leaf) {
        collect_leaf<Dim>(node, leaf_buckets, points, query, sq_radius, matches);
        return;
    }
    const auto  dim        = node.split_dim;
    const float split      = node.split_value;
    const float diff       = query[dim] - split;
    const auto  near_child = (diff < 0.0f) ? node.left : node.right;
    const auto  far_child  = (diff < 0.0f) ? node.right : node.left;

    collect_descend<Dim>(nodes, leaf_buckets, points, near_child, query, sq_radius, matches);
    if (diff * diff < sq_radius) {
        collect_descend<Dim>(nodes, leaf_buckets, points, far_child, query, sq_radius, matches);
    }
}

template <int Dim>
bool any_within_leaf(const TreeNode&                          node,
                     const LeafBucket&                        leaf_buckets,
                     const PointStore<Dim>&                   points,
                     const typename SearchKernel<Dim>::Point& query,
                     float                                    sq_radius) {
    const auto slice = leaf_buckets.view(node.bucket_offset, node.bucket_size);
    for (const auto& entry : slice) {
        if (points.generation(entry.index) != entry.gen || !points.is_live(entry.index)) {
            continue;
        }
        const float sq_dist = (points.point(entry.index) - query).squaredNorm();
        if (sq_dist < sq_radius) {
            return true;
        }
    }
    return false;
}

template <int Dim>
bool any_within_descend(const std::vector<TreeNode>&             nodes,
                        const LeafBucket&                        leaf_buckets,
                        const PointStore<Dim>&                   points,
                        std::uint32_t                            node_idx,
                        const typename SearchKernel<Dim>::Point& query,
                        float                                    sq_radius) {
    if (node_idx == TreeNode::INVALID) {
        return false;
    }
    const TreeNode& node = nodes[node_idx];
    if (node.is_leaf) {
        return any_within_leaf<Dim>(node, leaf_buckets, points, query, sq_radius);
    }
    const auto  dim        = node.split_dim;
    const float split      = node.split_value;
    const float diff       = query[dim] - split;
    const auto  near_child = (diff < 0.0f) ? node.left : node.right;
    const auto  far_child  = (diff < 0.0f) ? node.right : node.left;

    if (any_within_descend<Dim>(nodes, leaf_buckets, points, near_child, query, sq_radius)) {
        return true;
    }
    if (diff * diff < sq_radius) {
        return any_within_descend<Dim>(nodes, leaf_buckets, points, far_child, query, sq_radius);
    }
    return false;
}

} // namespace

template <int Dim>
SearchKernel<Dim>::SearchKernel(const std::vector<TreeNode>& nodes,
                                const LeafBucket&            leaf_buckets,
                                const PointStore<Dim>&       points)
    : nodes_(nodes), leaf_buckets_(leaf_buckets), points_(points) {}

template <int Dim>
auto SearchKernel<Dim>::search(std::uint32_t root,
                               const Point&  query,
                               std::size_t   k_max,
                               float         initial_sq_radius) const -> std::vector<Neighbor> {
    if (root == TreeNode::INVALID || k_max == 0) {
        return {};
    }

    std::vector<HeapEntry> heap;
    heap.reserve(k_max == std::numeric_limits<std::size_t>::max() ? 16 : k_max + 1);
    float worst_sq_dist = initial_sq_radius;

    std::array<float, Dim> min_sq_axis_dist{};
    descend<Dim>(
        nodes_, leaf_buckets_, points_, root, query, k_max, heap, worst_sq_dist, min_sq_axis_dist, 0.0f);

    std::sort(heap.begin(), heap.end(), [](const HeapEntry& lhs, const HeapEntry& rhs) {
        return lhs.sq_dist < rhs.sq_dist;
    });

    std::vector<Neighbor> result;
    result.reserve(heap.size());
    for (const auto& entry : heap) {
        result.push_back(Neighbor{points_.point(entry.index), entry.sq_dist});
    }
    return result;
}

template <int Dim>
auto SearchKernel<Dim>::collect_indices_within(std::uint32_t root, const Point& query, float sq_radius) const
    -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> result;
    if (root == TreeNode::INVALID) {
        return result;
    }
    collect_descend<Dim>(nodes_, leaf_buckets_, points_, root, query, sq_radius, result);
    return result;
}

template <int Dim>
bool SearchKernel<Dim>::any_within(std::uint32_t root, const Point& query, float sq_radius) const {
    if (root == TreeNode::INVALID) {
        return false;
    }
    return any_within_descend<Dim>(nodes_, leaf_buckets_, points_, root, query, sq_radius);
}

template class SearchKernel<2>;
template class SearchKernel<3>;
template class SearchKernel<4>;

} // namespace topiary::internal
