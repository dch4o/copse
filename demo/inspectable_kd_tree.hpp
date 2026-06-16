#pragma once

#include "copse/bbox.hpp"
#include "copse/impl/leaf_bucket.hpp"
#include "copse/impl/point_store.hpp"
#include "copse/impl/search_kernel.hpp"
#include "copse/impl/tree_builder.hpp"
#include "copse/impl/tree_node.hpp"
#include "copse/kd_tree.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace demo {

/// @brief Reassembles the kd-tree's internal building blocks (PointStore + LeafBucket
/// + TreeBuilder + SearchKernel + the node/cell pools) and mirrors KDTreeImpl's
/// orchestration, but keeps the node pool, leaf cells, and points fully readable
/// for visualization. The library is used as-is; nothing in copse changes.
/// @tparam Dim Point dimensionality.
template <int Dim>
class InspectableKdTree {
public:
    using Point    = typename copse::KDTree<Dim>::Point;
    using Config   = typename copse::KDTree<Dim>::Config;
    using Neighbor = typename copse::KDTree<Dim>::Neighbor;
    using TreeNode = copse::internal::TreeNode;

    /// @brief Construct an inspectable tree mirroring KDTreeImpl's storage and rebuild knobs.
    /// @param cfg Construction-time configuration.
    explicit InspectableKdTree(Config cfg)
        : cfg_(cfg)
        , points_(cfg.capacity)
        , leaf_buckets_(cfg.capacity * 2)
        , nodes_()
        , leaf_bboxes_()
        , root_bbox_{}
        , work_indices_()
        , builder_(nodes_,
                   leaf_buckets_,
                   leaf_bboxes_,
                   root_bbox_,
                   points_,
                   cfg.leaf_bucket_size,
                   cfg.alpha,
                   cfg.tombstone_threshold)
        , kernel_(nodes_, leaf_buckets_, points_) {}

    /// @brief Insert a batch with resolution dedup; the first point builds the root, the rest descend.
    /// @param points Points to insert.
    /// @return Number of input points actually written (dedup-rejects do not count).
    std::size_t insert(std::span<const Point> points) {
        const float sq_res   = cfg_.resolution * cfg_.resolution;
        std::size_t inserted = 0;
        for (const auto& point : points) {
            if (root_ != TreeNode::INVALID && kernel_.any_within(root_, point, sq_res)) {
                continue;
            }
            const auto index = points_.acquire(point);
            if (root_ == TreeNode::INVALID) {
                work_indices_.assign({index});
                root_ = builder_.rebuild(work_indices_);
            } else {
                builder_.insert_index(root_, index);
            }
            ++inserted;
        }
        if (inserted > 0) {
            builder_.maybe_partial_rebuild_full(root_);
        }
        return inserted;
    }

    /// @brief Release every live point inside the axis-aligned `box`.
    /// @param box Axis-aligned box to clear.
    /// @return Number of live points cleared.
    std::size_t box_delete(const copse::BBox<Dim>& box) {
        const std::size_t cleared = builder_.box_delete(root_, box);
        if (cleared > 0) {
            builder_.maybe_partial_rebuild(root_);
        }
        return cleared;
    }

    /// @brief Release every live point strictly outside the sphere of `radius` around `center`.
    /// @param center Sphere center.
    /// @param radius Sphere radius.
    /// @return Number of live points cleared.
    std::size_t radius_crop(const Point& center, float radius) {
        const std::size_t cleared = builder_.radius_crop(root_, center, radius);
        if (cleared > 0) {
            builder_.maybe_partial_rebuild(root_);
        }
        return cleared;
    }

    /// @brief k nearest neighbors of `query`, sorted ascending by squared distance.
    /// @param query Query point.
    /// @param k Number of neighbors to return.
    /// @return Up to `k` neighbors, sorted ascending by squared distance.
    std::vector<Neighbor> knn_search(const Point& query, std::size_t k) const {
        return kernel_.search(root_, query, k, std::numeric_limits<float>::infinity());
    }

    /// @brief All neighbors of `query` within `radius`, sorted ascending by squared distance.
    /// @param query Query point.
    /// @param radius Search radius (Euclidean).
    /// @return Every neighbor within `radius`, sorted ascending by squared distance.
    std::vector<Neighbor> radius_search(const Point& query, float radius) const {
        return kernel_.search(root_, query, std::numeric_limits<std::size_t>::max(), radius * radius);
    }

    /// @brief Up to `k` nearest neighbors of `query` within `radius`.
    /// @param query Query point.
    /// @param k Neighbor cap.
    /// @param radius Search radius (Euclidean).
    /// @return Up to `k` neighbors within `radius`, sorted ascending by squared distance.
    std::vector<Neighbor> hybrid_search(const Point& query, std::size_t k, float radius) const {
        return kernel_.search(root_, query, k, radius * radius);
    }

    /// @brief Force a from-scratch rebuild of the tree topology; liveness unchanged.
    void rebuild_all() {
        nodes_.clear();
        leaf_buckets_.clear();
        leaf_bboxes_.clear();
        work_indices_.clear();
        points_.for_each_live([this](std::uint32_t index, const Point&) { work_indices_.push_back(index); });
        root_ = work_indices_.empty() ? TreeNode::INVALID : builder_.rebuild(work_indices_);
    }

    /// @brief Live point count.
    /// @return Number of live points currently stored.
    std::size_t size() const { return points_.size(); }

    /// @brief Fixed capacity supplied at construction.
    /// @return The configured capacity.
    std::size_t capacity() const { return points_.capacity(); }

    // --- Introspection for visualization (all read-only, library untouched) ---

    /// @brief Root node index of the current topology.
    /// @return Root node index, or `TreeNode::INVALID` when empty.
    std::uint32_t root() const { return root_; }

    /// @brief Read-only view of the node pool.
    /// @return The node pool.
    const std::vector<TreeNode>& nodes() const { return nodes_; }

    /// @brief Read-only view of the per-leaf bounding boxes.
    /// @return The leaf bounding-box pool.
    const std::vector<copse::BBox<Dim>>& leaf_bboxes() const { return leaf_bboxes_; }

    /// @brief Whole-tree bounding box.
    /// @return The root bounding box.
    const copse::BBox<Dim>& root_bbox() const { return root_bbox_; }

    /// @brief Read-only view of the point store.
    /// @return The point store.
    const copse::internal::PointStore<Dim>& points() const { return points_; }

    /// @brief Read-only view of the leaf bucket pool.
    /// @return The leaf bucket pool.
    const copse::internal::LeafBucket& leaf_buckets() const { return leaf_buckets_; }

private:
    Config                               cfg_;
    copse::internal::PointStore<Dim>   points_;
    copse::internal::LeafBucket        leaf_buckets_;
    std::vector<TreeNode>                nodes_;
    std::vector<copse::BBox<Dim>>      leaf_bboxes_;
    copse::BBox<Dim>                   root_bbox_;
    std::vector<std::uint32_t>           work_indices_;
    copse::internal::TreeBuilder<Dim>  builder_;
    copse::internal::SearchKernel<Dim> kernel_;
    std::uint32_t                        root_ = TreeNode::INVALID;
};

} // namespace demo
