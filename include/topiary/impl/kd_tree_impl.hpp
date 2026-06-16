#pragma once

#include "topiary/impl/leaf_bucket.hpp"
#include "topiary/impl/point_store.hpp"
#include "topiary/impl/search_kernel.hpp"
#include "topiary/impl/tree_builder.hpp"
#include "topiary/impl/tree_node.hpp"
#include "topiary/kd_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace topiary::internal {

/// @brief PIMPL body of KDTree<Dim>; owns all storage and orchestrates the helpers.
/// Single-writer; not safe to call mutators concurrently with any other method.
/// @tparam Dim Point dimensionality.
template <int Dim>
class KDTreeImpl {
public:
    using Config   = typename topiary::KDTree<Dim>::Config;
    using Point    = typename topiary::KDTree<Dim>::Point;
    using Neighbor = typename topiary::KDTree<Dim>::Neighbor;

    /// @brief Construct with validated configuration.
    /// @throws std::invalid_argument On any Config precondition violation.
    explicit KDTreeImpl(Config cfg);

    /// @copydoc topiary::KDTree::insert
    std::size_t insert(std::span<const Point> points);

    /// @copydoc topiary::KDTree::remove
    std::size_t remove(std::span<const Point> queries);

    /// @copydoc topiary::KDTree::knn_search
    std::vector<Neighbor> knn_search(const Point& query, std::size_t k) const;

    /// @copydoc topiary::KDTree::radius_search
    std::vector<Neighbor> radius_search(const Point& query, float radius) const;

    /// @copydoc topiary::KDTree::hybrid_search
    std::vector<Neighbor> hybrid_search(const Point& query, std::size_t k, float radius) const;

    /// @copydoc topiary::KDTree::delete_in_box
    std::size_t delete_in_box(const Point& min_corner, const Point& max_corner);

    /// @copydoc topiary::KDTree::delete_in_boxes
    std::size_t delete_in_boxes(std::span<const BBox<Dim>> boxes);

    /// @copydoc topiary::KDTree::delete_outside_radius
    std::size_t delete_outside_radius(const Point& center, float r);

    /// @copydoc topiary::KDTree::size
    std::size_t size() const noexcept;

    /// @copydoc topiary::KDTree::capacity
    std::size_t capacity() const noexcept;

    /// @copydoc topiary::KDTree::rebuild_all
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

} // namespace topiary::internal
