// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
#pragma once

#include "copse/impl/leaf_bucket.hpp"
#include "copse/impl/point_store.hpp"
#include "copse/impl/point_traits.hpp"
#include "copse/impl/tree_node.hpp"
#include "copse/kd_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace copse::internal {

/// @brief Single bounded traversal kernel shared by knn / radius / hybrid searches.
/// @tparam Dim Point dimensionality.
template <int Dim>
class SearchKernel {
public:
    using Point    = copse::Point<Dim>;
    using Neighbor = typename copse::KDTree<Dim>::Neighbor;

    /// @brief Construct a kernel bound to the supplied storage.
    /// @param nodes Node pool to traverse.
    /// @param leaf_buckets Leaf bucket pool.
    /// @param points Point store backing the indices.
    SearchKernel(const std::vector<TreeNode>& nodes,
                 const LeafBucket&            leaf_buckets,
                 const PointStore<Dim>&       points);

    /// @brief Run a unified bounded search; returns neighbors sorted ascending by squared distance.
    /// @param root Subtree root to traverse; `TreeNode::INVALID` yields no results.
    /// @param query Query point.
    /// @param k_max Neighbor cap; `SIZE_MAX` is unbounded.
    /// @param initial_sq_radius Squared radius bound; `+inf` is pure kNN.
    /// @return Matching neighbors, sorted ascending by squared distance.
    std::vector<Neighbor>
    search(std::uint32_t root, const Point& query, std::size_t k_max, float initial_sq_radius) const;

    /// @brief Collect every live index strictly within `sq_radius` of `query`; order unspecified.
    /// @param root Subtree root to traverse.
    /// @param query Query point.
    /// @param sq_radius Squared search radius.
    /// @return Indices of every live point strictly within `sq_radius`.
    std::vector<std::uint32_t>
    collect_indices_within(std::uint32_t root, const Point& query, float sq_radius) const;

    /// @brief Early-exit predicate: true iff any live point lies strictly within `sq_radius` of `query`.
    /// @param root Subtree root to traverse.
    /// @param query Query point.
    /// @param sq_radius Squared search radius.
    /// @return True iff at least one live point lies strictly within `sq_radius`.
    bool any_within(std::uint32_t root, const Point& query, float sq_radius) const;

private:
    const std::vector<TreeNode>& nodes_;
    const LeafBucket&            leaf_buckets_;
    const PointStore<Dim>&       points_;
};

} // namespace copse::internal
