#pragma once

#include "pkd_tree/fixed_kd_tree.hpp"
#include "pkd_tree/impl/leaf_bucket.hpp"
#include "pkd_tree/impl/point_store.hpp"
#include "pkd_tree/impl/search_kernel.hpp"
#include "pkd_tree/impl/tree_builder.hpp"
#include "pkd_tree/impl/tree_node.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pkd_tree::internal {

/// @brief PIMPL body of FixedKdTree<Dim>; owns all storage and orchestrates the helpers.
/// Single-writer; not safe to call mutators concurrently with any other method.
template <int Dim>
class FixedKdTreeImpl {
public:
    using Config   = typename pkd_tree::FixedKdTree<Dim>::Config;
    using Point    = typename pkd_tree::FixedKdTree<Dim>::Point;
    using Neighbor = typename pkd_tree::FixedKdTree<Dim>::Neighbor;

    /// @brief Construct with validated configuration.
    /// @throws std::invalid_argument On any Config precondition violation.
    explicit FixedKdTreeImpl(Config cfg);

    /// @copydoc pkd_tree::FixedKdTree::insert
    std::size_t insert(std::span<const Point> points);

    /// @copydoc pkd_tree::FixedKdTree::remove
    std::size_t remove(std::span<const Point> queries);

    /// @copydoc pkd_tree::FixedKdTree::knn_search
    std::vector<Neighbor> knn_search(const Point& query, std::size_t k) const;

    /// @copydoc pkd_tree::FixedKdTree::radius_search
    std::vector<Neighbor> radius_search(const Point& query, float radius) const;

    /// @copydoc pkd_tree::FixedKdTree::hybrid_search
    std::vector<Neighbor> hybrid_search(const Point& query, std::size_t k, float radius) const;

    std::size_t size() const noexcept;

    std::size_t capacity() const noexcept;

    /// @copydoc pkd_tree::FixedKdTree::rebuild_all
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

} // namespace pkd_tree::internal
