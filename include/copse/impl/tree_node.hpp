// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
#pragma once

#include "copse/bbox.hpp"
#include "copse/point_traits.hpp"

#include <cstdint>

namespace copse::internal {

/// @brief Tagged-union node: internal split node or bucketed leaf, selected by `is_leaf`.
/// The anonymous inner structs are a widely supported GCC/Clang extension; pedantic
/// warning is suppressed locally so callers can write `node.split_dim` / `node.bucket_offset`
/// without an `internal.`/`leaf.` middle hop.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
struct TreeNode {
    bool          is_leaf             = true; /// True for a bucketed leaf; false for an internal split node.
    std::uint32_t subtree_live_count  = 0;    /// Live points in the subtree (excludes tombstones).
    std::uint32_t subtree_total_count = 0;    /// Includes tombstones; always >= subtree_live_count.

    union {
        struct {
            std::uint8_t  split_dim;   /// Dimension index split on (0..D-1).
            float         split_value; /// Split-plane coordinate on `split_dim`.
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

    /// @brief Explicit pad to 32 B (cache density on hot kNN/radius descent).
    std::uint32_t reserved_ = 0;

    static constexpr std::uint32_t INVALID = ~std::uint32_t{0}; /// Sentinel for unset child indices.

    /// @brief Default-constructs as an empty leaf node.
    TreeNode() noexcept : bucket_offset(0), bucket_size(0), bucket_capacity(0), leaf_bbox_idx(INVALID) {}
};
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace copse::internal
