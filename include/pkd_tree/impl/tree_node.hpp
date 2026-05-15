#pragma once

#include "pkd_tree/impl/point_traits.hpp"

#include <cstdint>

namespace pkd_tree::internal {

/// @brief Axis-aligned bounding box used for leaf-level pruning and the whole-tree root extent.
template <int Dim>
struct BBox {
    detail::PointType<Dim> min_corner; /// Per-axis lower bound (inclusive).
    detail::PointType<Dim> max_corner; /// Per-axis upper bound (inclusive).
};

/// @brief Tagged-union node: internal split node or bucketed leaf, selected by `is_leaf`.
/// The anonymous inner structs are a widely supported GCC/Clang extension; pedantic
/// warning is suppressed locally so callers can write `node.split_dim` / `node.bucket_offset`
/// without an `internal.`/`leaf.` middle hop.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
struct TreeNode {
    bool          is_leaf             = true;
    std::uint32_t subtree_live_count  = 0; /// Live points in the subtree (excludes tombstones).
    std::uint32_t subtree_total_count = 0; /// Includes tombstones; always >= subtree_live_count.

    union {
        struct {
            std::uint8_t  split_dim;   /// Dimension index split on (0..D-1).
            float         split_value;
            std::uint32_t left;        /// Child node index; INVALID when unset.
            std::uint32_t right;       /// Child node index; INVALID when unset.
        };
        struct {
            std::uint32_t bucket_offset;   /// Slice start in the LeafBucket pool.
            std::uint16_t bucket_size;     /// Live + stale entries in the bucket; <= bucket_capacity.
            std::uint16_t bucket_capacity; /// Fixed at 2 * leaf_bucket_size.
            std::uint32_t leaf_bbox_idx;   /// Index into the per-tree leaf BBox vector.
        };
    };

    /// @brief Explicit pad to 32 B (cache density on hot kNN/radius descent); see static_assert below.
    std::uint32_t reserved_ = 0;

    static constexpr std::uint32_t INVALID = ~std::uint32_t{0}; /// Sentinel for unset child indices.

    /// @brief Default-constructs as an empty leaf node.
    TreeNode() noexcept
        : bucket_offset(0), bucket_size(0), bucket_capacity(0), leaf_bbox_idx(INVALID) {}
};
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static_assert(sizeof(TreeNode) == 32, "TreeNode must stay 32 B for cache density on descent");

} // namespace pkd_tree::internal
