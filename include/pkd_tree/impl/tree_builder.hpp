#pragma once

#include "pkd_tree/impl/leaf_bucket.hpp"
#include "pkd_tree/impl/point_store.hpp"
#include "pkd_tree/impl/tree_node.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pkd_tree::internal {

/// @brief Builds and partially-rebuilds the kd-tree topology over an externally-owned node pool.
template <int Dim>
class TreeBuilder {
public:
    /// @brief Construct a builder bound to the supplied storage and rebuild knobs.
    TreeBuilder(std::vector<TreeNode>&   node_pool,
                LeafBucket&              leaf_buckets,
                std::vector<BBox<Dim>>&  leaf_bboxes,
                BBox<Dim>&               root_bbox,
                const PointStore<Dim>&   points,
                std::size_t              leaf_bucket_size,
                float                    alpha,
                float                    tombstone_threshold);

    /// @brief From-scratch rebuild; partitions live_indices in place and returns the new root index.
    /// Populates each leaf's BBox into `leaf_bboxes_` and the whole-tree extent into `root_bbox_`.
    std::uint32_t rebuild(std::span<std::uint32_t> live_indices);

    /// @brief Recursive top-down sweep; rebuilds every violator subtree in place.
    /// After children of a non-violator node finish, the node is re-checked because a child
    /// rebuild may have shifted its left/right ratio. Triggers: unbalanced (max_child/total > alpha),
    /// tombstoned ((total-live)/total >= tombstone_threshold), and leaves with bucket_size > 2 * B.
    void maybe_partial_rebuild(std::uint32_t current_root);

    /// @brief Insert a single index into the existing topology and expand the touched leaf's BBox.
    /// Bumps `subtree_*_count` along the descent path and expands `root_bbox_` via cwiseMin/Max.
    void insert_index(std::uint32_t root, std::uint32_t index);

    /// @brief Tombstone an index in its leaf and decrement live counts on the descent path.
    /// Caller MUST invoke this BEFORE `PointStore::release(index)` so descent can read the coord.
    void tombstone_index(std::uint32_t root, std::uint32_t index);

private:
    std::vector<TreeNode>&   node_pool_;
    LeafBucket&              leaf_buckets_;
    std::vector<BBox<Dim>>&  leaf_bboxes_;
    BBox<Dim>&               root_bbox_;
    const PointStore<Dim>&   points_;
    std::size_t              leaf_bucket_size_;
    float                    alpha_;
    float                    tombstone_threshold_;
};

} // namespace pkd_tree::internal
