#pragma once

#include "pkd_tree/fixed_kd_tree.hpp"
#include "pkd_tree/impl/leaf_bucket.hpp"
#include "pkd_tree/impl/point_store.hpp"
#include "pkd_tree/impl/point_traits.hpp"
#include "pkd_tree/impl/tree_node.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pkd_tree::internal {

/// @brief Single bounded traversal kernel shared by knn / radius / hybrid searches.
/// Holds non-owning references to the node pool, leaf-bucket pool, and point store.
template <int Dim>
class SearchKernel {
public:
    using Point    = detail::PointType<Dim>;
    using Neighbor = typename pkd_tree::FixedKdTree<Dim>::Neighbor;

    /// @brief Construct a kernel bound to the supplied storage.
    SearchKernel(const std::vector<TreeNode>& node_pool,
                 const LeafBucket&            leaf_buckets,
                 const PointStore<Dim>&       points);

    /// @brief Run a unified bounded search; returns neighbors sorted ascending by squared distance.
    /// `k_max == SIZE_MAX` is unbounded; `initial_sq_radius == +inf` is pure kNN.
    std::vector<Neighbor>
    search(std::uint32_t root, const Point& query, std::size_t k_max, float initial_sq_radius) const;

    /// @brief Collect every live index strictly within `sq_radius` of `query`; order unspecified.
    std::vector<std::uint32_t>
    collect_indices_within(std::uint32_t root, const Point& query, float sq_radius) const;

    /// @brief Early-exit predicate: true iff any live point lies strictly within `sq_radius` of `query`.
    /// Allocation-free; used in the dedup hot path on every `insert` candidate.
    bool any_within(std::uint32_t root, const Point& query, float sq_radius) const;

private:
    const std::vector<TreeNode>& node_pool_;
    const LeafBucket&            leaf_buckets_;
    const PointStore<Dim>&       points_;
};

} // namespace pkd_tree::internal
