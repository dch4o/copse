#pragma once

#include "topiary/bbox.hpp"
#include "topiary/impl/point_traits.hpp"

#include <Eigen/Core>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace topiary {

namespace internal {
/// @brief PIMPL implementation type, defined in `impl/kd_tree_impl.hpp`.
/// @tparam Dim Point dimensionality.
template <int Dim>
class KDTreeImpl;
} // namespace internal

/// @brief Fixed-capacity, mutable kd-tree of float points with FIFO eviction and resolution-based dedup.
/// Single-writer; the caller is responsible for serializing access.
/// @tparam Dim Point dimensionality.
template <int Dim>
    requires detail::SupportedDim<Dim>
class KDTree {
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
        Point coord;   /// Neighbor point coordinates.
        float sq_dist; /// Squared Euclidean distance from the query.
    };

    /// @brief Construct a kd-tree with the supplied configuration.
    /// @param cfg Construction-time configuration.
    /// @throws std::invalid_argument If any precondition on `cfg` is violated.
    explicit KDTree(Config cfg);

    /// @brief Destroy the tree and release all storage.
    ~KDTree();

    KDTree(const KDTree&)            = delete;
    KDTree& operator=(const KDTree&) = delete;

    /// @brief Move-construct, transferring ownership of the implementation.
    KDTree(KDTree&&) noexcept;

    /// @brief Move-assign, transferring ownership of the implementation.
    /// @return Reference to this tree.
    KDTree& operator=(KDTree&&) noexcept;

    /// @brief Insert a batch with intra-batch and tree-side dedup; FIFO-evicts when full.
    /// @param points Points to insert.
    /// @return Number of input points actually written (FIFO evictions count; dedup-rejects do not).
    std::size_t insert(std::span<const Point> points);

    /// @brief Remove every live point within `Config::resolution` of each query.
    /// @param queries Query points whose neighborhoods are cleared.
    /// @return Total number of live points cleared across all queries.
    std::size_t remove(std::span<const Point> queries);

    /// @brief k nearest neighbors of `query`, sorted ascending by squared distance.
    /// @param query Query point.
    /// @param k Number of neighbors to return.
    /// @return Up to `k` neighbors, sorted ascending by squared distance.
    /// @throws std::invalid_argument If `k == 0`.
    std::vector<Neighbor> knn_search(const Point& query, std::size_t k) const;

    /// @brief All neighbors of `query` within `radius` (linear), sorted ascending by squared distance.
    /// @param query Query point.
    /// @param radius Search radius (Euclidean).
    /// @return Every neighbor within `radius`, sorted ascending by squared distance.
    /// @throws std::invalid_argument If `radius < 0`.
    std::vector<Neighbor> radius_search(const Point& query, float radius) const;

    /// @brief Up to `k` nearest neighbors of `query` within `radius`, sorted ascending by squared distance.
    /// @param query Query point.
    /// @param k Neighbor cap.
    /// @param radius Search radius (Euclidean).
    /// @return Up to `k` neighbors within `radius`, sorted ascending by squared distance.
    /// @throws std::invalid_argument If `k == 0` or `radius < 0`.
    std::vector<Neighbor> hybrid_search(const Point& query, std::size_t k, float radius) const;

    /// @brief Release every live point inside the axis-aligned `box`.
    /// @param box Axis-aligned box to clear.
    /// @return Number of live points cleared.
    std::size_t delete_box(const BBox<Dim>& box);

    /// @brief Release every live point inside any of `boxes`, with a single end-of-batch rebuild.
    /// @param boxes Axis-aligned boxes to clear.
    /// @return Total number of live points cleared across all boxes (points in overlaps count once).
    std::size_t delete_boxes(std::span<const BBox<Dim>> boxes);

    /// @brief Release every live point strictly outside the sphere of radius `r` around `center`.
    /// @param center Sphere center.
    /// @param r Sphere radius.
    /// @return Number of live points cleared.
    std::size_t delete_outside_radius(const Point& center, float r);

    /// @brief Live point count.
    /// @return Number of live points currently stored.
    std::size_t size() const noexcept;

    /// @brief Fixed capacity supplied at construction.
    /// @return The configured capacity N.
    std::size_t capacity() const noexcept;

    /// @brief Force a from-scratch rebuild of the tree topology; liveness unchanged.
    void rebuild_all();

private:
    std::unique_ptr<internal::KDTreeImpl<Dim>> impl_;
};

/// @name Convenience aliases for the supported D values.
/// @{
using KDTree2 = KDTree<2>;
using KDTree3 = KDTree<3>;
using KDTree4 = KDTree<4>;
/// @}

} // namespace topiary
