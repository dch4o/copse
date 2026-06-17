// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
#pragma once

#include "copse/impl/leaf_bucket.hpp"
#include "copse/impl/point_store.hpp"
#include "copse/impl/tree_node.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace copse::internal {

/// @brief Builds and partially-rebuilds the kd-tree topology over an externally-owned node pool.
/// @tparam Dim Point dimensionality.
template <int Dim>
class TreeBuilder {
public:
    using Point = copse::Point<Dim>;

    /// @brief Construct a builder bound to the supplied storage and rebuild knobs.
    /// @param nodes Node pool to build into.
    /// @param leaf_buckets Leaf bucket pool.
    /// @param leaf_bboxes Per-leaf bounding-box pool.
    /// @param root_bbox Whole-tree bounding box, refreshed on rebuild.
    /// @param points Point store backing the indices.
    /// @param leaf_bucket_size Target points per leaf before a split is preferred.
    /// @param alpha Subtree-imbalance trigger; 0.5 < alpha < 1.
    /// @param tombstone_threshold Per-subtree tombstone fraction triggering partial rebuild.
    TreeBuilder(std::vector<TreeNode>&  nodes,
                LeafBucket&             leaf_buckets,
                std::vector<BBox<Dim>>& leaf_bboxes,
                BBox<Dim>&              root_bbox,
                PointStore<Dim>&        points,
                std::size_t             leaf_bucket_size,
                float                   alpha,
                float                   tombstone_threshold);

    /// @brief From-scratch rebuild; partitions `live_indices` in place.
    /// @param live_indices Live point indices to partition (reordered in place).
    /// @return The new root node index, or `TreeNode::INVALID` if empty.
    std::uint32_t rebuild(std::span<std::uint32_t> live_indices);

    /// @brief Scoped sweep over `modified_nodes_`; rebuilds every violator subtree in place.
    /// @param current_root Current tree root.
    void maybe_partial_rebuild(std::uint32_t current_root);

    /// @brief Full top-down rebuild sweep over the whole tree (ignores `modified_nodes_`).
    /// @param current_root Current tree root.
    void maybe_partial_rebuild_full(std::uint32_t current_root);

    /// @brief Insert a single index into the existing topology and expand the touched leaf's BBox.
    /// @param root Tree root to descend from.
    /// @param index Point index to insert.
    void insert_index(std::uint32_t root, std::uint32_t index);

    /// @brief Tombstone an index in its leaf and decrement live counts on the descent path.
    /// Caller MUST invoke this BEFORE `PointStore::release(index)` so descent can read the coord.
    /// @param root Tree root to descend from.
    /// @param index Point index to tombstone.
    void tombstone_index(std::uint32_t root, std::uint32_t index);

    /// @brief Release every live index inside the axis-aligned `box`.
    /// @param root Tree root to descend from.
    /// @param box Axis-aligned box to clear.
    /// @return Count of indices cleared.
    std::size_t box_delete(std::uint32_t root, const BBox<Dim>& box);

    /// @brief Release every live index strictly outside the r-sphere around `center`.
    /// @param root Tree root to descend from.
    /// @param center Sphere center.
    /// @param r Sphere radius.
    /// @return Count of indices cleared.
    std::size_t radius_crop(std::uint32_t root, const Point& center, float r);

private:
    std::vector<TreeNode>&  nodes_;
    LeafBucket&             leaf_buckets_;
    std::vector<BBox<Dim>>& leaf_bboxes_;
    BBox<Dim>&              root_bbox_;
    PointStore<Dim>&        points_;
    std::size_t             leaf_bucket_size_;
    float                   alpha_;
    float                   tombstone_threshold_;

    /// Internal nodes whose subtree counts changed since the last `maybe_partial_rebuild`.
    std::vector<std::uint32_t> modified_nodes_;
};

} // namespace copse::internal
