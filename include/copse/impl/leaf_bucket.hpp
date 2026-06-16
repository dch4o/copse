#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace copse::internal {

/// @brief Generation-stamped index stored in each leaf bucket slot.
struct BucketEntry {
    std::uint32_t index; /// Point index into the PointStore.
    std::uint32_t gen;   /// Generation stamp captured when the entry was written.
};

/// @brief Flat backing store for kd-tree leaf buckets; fixed-cap slices, no in-place compaction.
class LeafBucket {
public:
    /// @brief Construct with reserved capacity.
    /// @param initial_entries Number of entry slots to reserve.
    explicit LeafBucket(std::size_t initial_entries);

    /// @brief Allocate a tail slice of entries; returns the starting offset.
    /// @param capacity Number of slots to allocate.
    /// @return Starting offset of the allocated slice.
    std::uint32_t allocate(std::uint16_t capacity);

    /// @brief Read-only view of a bucket slice.
    /// @param offset Slice start offset.
    /// @param size Number of entries in the slice.
    /// @return Read-only span over the slice.
    std::span<const BucketEntry> view(std::uint32_t offset, std::uint16_t size) const;

    /// @brief Mutable view of a bucket slice.
    /// @param offset Slice start offset.
    /// @param size Number of entries in the slice.
    /// @return Mutable span over the slice.
    std::span<BucketEntry> view(std::uint32_t offset, std::uint16_t size);

    /// @brief Append an entry to a bucket (caller triggers leaf split on a full bucket).
    /// @param offset Bucket slice start in the pool.
    /// @param[in,out] size Current bucket size; incremented on success.
    /// @param capacity Bucket slot capacity.
    /// @param entry Entry to append.
    /// @return False if `size == capacity` (the bucket is full); true on success.
    bool push(std::uint32_t offset, std::uint16_t& size, std::uint16_t capacity, BucketEntry entry);

    /// @brief Reset to empty without releasing the reserved capacity.
    void clear() noexcept;

private:
    std::vector<BucketEntry> data_;
};

} // namespace copse::internal
