// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
#pragma once

#include "copse/impl/leaf_bucket.hpp"
#include "copse/impl/point_store.hpp"
#include "copse/impl/search_kernel.hpp"
#include "copse/impl/tree_builder.hpp"
#include "copse/impl/tree_node.hpp"
#include "copse/kd_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace copse::internal {

/// @brief PIMPL body of KDTree<Dim>; owns all storage and orchestrates the helpers.
/// Single-writer; not safe to call mutators concurrently with any other method.
/// @tparam Dim Point dimensionality.
template <int Dim>
class KDTreeImpl {
public:
    using Config   = typename copse::KDTree<Dim>::Config;
    using Point    = typename copse::KDTree<Dim>::Point;
    using Neighbor = typename copse::KDTree<Dim>::Neighbor;

    /// @brief Construct with validated configuration.
    /// @param cfg Construction-time configuration.
    /// @throws std::invalid_argument On any Config precondition violation.
    explicit KDTreeImpl(Config cfg);

    /// @copydoc copse::KDTree::insert
    std::size_t insert(std::span<const Point> points);

    /// @copydoc copse::KDTree::remove
    std::size_t remove(std::span<const Point> queries);

    /// @copydoc copse::KDTree::knn_search
    std::vector<Neighbor> knn_search(const Point& query, std::size_t k) const;

    /// @copydoc copse::KDTree::radius_search
    std::vector<Neighbor> radius_search(const Point& query, float radius) const;

    /// @copydoc copse::KDTree::hybrid_search
    std::vector<Neighbor> hybrid_search(const Point& query, std::size_t k, float radius) const;

    /// @copydoc copse::KDTree::box_delete(std::span<const BBox<Dim>>)
    std::size_t box_delete(std::span<const BBox<Dim>> boxes);

    /// @copydoc copse::KDTree::radius_crop
    std::size_t radius_crop(const Point& center, float r);

    /// @copydoc copse::KDTree::size
    std::size_t size() const noexcept;

    /// @copydoc copse::KDTree::capacity
    std::size_t capacity() const noexcept;

    /// @copydoc copse::KDTree::rebuild_all
    void rebuild_all();

private:
    Config                     cfg_;
    PointStore<Dim>            points_;
    LeafBucket                 leaf_buckets_;
    std::vector<TreeNode>      nodes_;
    std::vector<BBox<Dim>>     leaf_bboxes_;
    BBox<Dim>                  root_bbox_{};
    std::vector<std::uint32_t> work_indices_;
    std::uint32_t              root_ = TreeNode::INVALID;
    TreeBuilder<Dim>           builder_;
    SearchKernel<Dim>          kernel_;
};

} // namespace copse::internal
