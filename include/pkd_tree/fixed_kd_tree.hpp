#pragma once

#include "pkd_tree/impl/point_traits.hpp"

#include <Eigen/Core>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace pkd_tree {

namespace internal {
/// @brief PIMPL implementation type, defined in `impl/fixed_kd_tree_impl.hpp`.
template <int Dim>
class FixedKdTreeImpl;
} // namespace internal

/// @brief Fixed-capacity, mutable kd-tree of float points with FIFO eviction and resolution-based dedup.
/// Single-writer; the caller is responsible for serializing access.
template <int Dim>
    requires detail::KdDim<Dim>
class FixedKdTree {
public:
    using Point = detail::PointType<Dim>; /// Canonical Eigen column-vector point type.

    /// @brief Construction-time configuration; preconditions enforced at construction.
    struct Config {
        std::size_t capacity   = 0;    /// Fixed capacity N; required > 0.
        float       resolution = 0.0f; /// Euclidean dedup radius; required > 0.
        std::size_t leaf_bucket_size =
            detail::default_leaf_bucket_size_v<Dim>; /// Per-leaf points before a split is preferred during
                                                     /// rebuild.
        float alpha = 0.7f;                          /// Subtree-imbalance trigger; 0.5 < α < 1.
        float tombstone_threshold =
            0.25f; /// Per-subtree tombstone fraction triggering partial rebuild; in [0, 1].
    };

    /// @brief A single neighbor result; distance is squared by contract.
    struct Neighbor {
        Point coord;
        float sq_dist;
    };

    /// @brief Construct a kd-tree with the supplied configuration.
    /// @throws std::invalid_argument If any precondition on `cfg` is violated.
    explicit FixedKdTree(Config cfg);

    ~FixedKdTree();

    FixedKdTree(const FixedKdTree&)            = delete;
    FixedKdTree& operator=(const FixedKdTree&) = delete;
    FixedKdTree(FixedKdTree&&) noexcept;
    FixedKdTree& operator=(FixedKdTree&&) noexcept;

    /// @brief Insert a batch with intra-batch and tree-side dedup; FIFO-evicts when full.
    /// @return Number of input points actually written (FIFO evictions count; dedup-rejects do not).
    std::size_t insert(std::span<const Point> points);

    /// @brief Remove every live point within `Config::resolution` of each query.
    /// @return Total number of live points cleared across all queries.
    std::size_t remove(std::span<const Point> queries);

    /// @brief k nearest neighbors of `query`, sorted ascending by squared distance.
    /// @throws std::invalid_argument If `k == 0`.
    std::vector<Neighbor> knn_search(const Point& query, std::size_t k) const;

    /// @brief All neighbors of `query` within `radius` (linear), sorted ascending by squared distance.
    /// @throws std::invalid_argument If `radius < 0`.
    std::vector<Neighbor> radius_search(const Point& query, float radius) const;

    /// @brief Up to `k` nearest neighbors of `query` within `radius`, sorted ascending by squared distance.
    /// @throws std::invalid_argument If `k == 0` or `radius < 0`.
    std::vector<Neighbor> hybrid_search(const Point& query, std::size_t k, float radius) const;

    std::size_t size() const noexcept;

    /// @brief Fixed capacity supplied at construction.
    std::size_t capacity() const noexcept;

    /// @brief Force a from-scratch rebuild of the tree topology; liveness unchanged.
    void rebuild_all();

private:
    std::unique_ptr<internal::FixedKdTreeImpl<Dim>> impl_;
};

/// @name Convenience aliases for the supported D values.
/// @{
using FixedKdTree2 = FixedKdTree<2>;
using FixedKdTree3 = FixedKdTree<3>;
using FixedKdTree4 = FixedKdTree<4>;
/// @}

} // namespace pkd_tree
