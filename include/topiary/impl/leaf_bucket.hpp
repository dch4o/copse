#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace topiary::internal {

/// @brief Generation-stamped index stored in each leaf bucket slot.
struct BucketEntry {
    std::uint32_t index;
    std::uint32_t gen;
};

static_assert(sizeof(BucketEntry) == 8, "BucketEntry must stay 8 B for bucket footprint accounting");

/// @brief Flat backing store for kd-tree leaf buckets; fixed-cap slices, no in-place compaction.
class LeafBucket {
public:
    /// @brief Construct with reserved capacity.
    explicit LeafBucket(std::size_t initial_entries);

    /// @brief Allocate a tail slice of entries; returns the starting offset.
    std::uint32_t allocate(std::uint16_t capacity);

    /// @brief Read-only view of a bucket slice.
    std::span<const BucketEntry> view(std::uint32_t offset, std::uint16_t size) const;

    /// @brief Mutable view of a bucket slice.
    std::span<BucketEntry> view(std::uint32_t offset, std::uint16_t size);

    /// @brief Append an entry to a bucket (caller triggers leaf split on a full bucket).
    /// @param[in,out] size Current bucket size; incremented on success.
    /// @return False if `size == capacity` (the bucket is full); true on success.
    bool push(std::uint32_t offset, std::uint16_t& size, std::uint16_t capacity, BucketEntry entry);

    /// @brief Reset to empty without releasing the reserved capacity.
    void clear() noexcept;

private:
    std::vector<BucketEntry> data_;
};

} // namespace topiary::internal
