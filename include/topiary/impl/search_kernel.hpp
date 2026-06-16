#pragma once

#include "topiary/impl/leaf_bucket.hpp"
#include "topiary/impl/point_store.hpp"
#include "topiary/impl/point_traits.hpp"
#include "topiary/impl/tree_node.hpp"
#include "topiary/kd_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace topiary::internal {

/// @brief Single bounded traversal kernel shared by knn / radius / hybrid searches.
/// @tparam Dim Point dimensionality.
template <int Dim>
class SearchKernel {
public:
    using Point    = detail::PointType<Dim>;
    using Neighbor = typename topiary::KDTree<Dim>::Neighbor;

    /// @brief Construct a kernel bound to the supplied storage.
    SearchKernel(const std::vector<TreeNode>& nodes,
                 const LeafBucket&            leaf_buckets,
                 const PointStore<Dim>&       points);

    /// @brief Run a unified bounded search; returns neighbors sorted ascending by squared distance.
    /// @param k_max Neighbor cap; `SIZE_MAX` is unbounded.
    /// @param initial_sq_radius Squared radius bound; `+inf` is pure kNN.
    std::vector<Neighbor>
    search(std::uint32_t root, const Point& query, std::size_t k_max, float initial_sq_radius) const;

    /// @brief Collect every live index strictly within `sq_radius` of `query`; order unspecified.
    std::vector<std::uint32_t>
    collect_indices_within(std::uint32_t root, const Point& query, float sq_radius) const;

    /// @brief Early-exit predicate: true iff any live point lies strictly within `sq_radius` of `query`.
    bool any_within(std::uint32_t root, const Point& query, float sq_radius) const;

private:
    const std::vector<TreeNode>& nodes_;
    const LeafBucket&            leaf_buckets_;
    const PointStore<Dim>&       points_;
};

} // namespace topiary::internal
