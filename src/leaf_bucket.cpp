#include "copse/impl/leaf_bucket.hpp"

namespace copse::internal {

static_assert(sizeof(BucketEntry) == 8, "BucketEntry must stay 8 B for bucket footprint accounting");

LeafBucket::LeafBucket(std::size_t initial_entries) {
    data_.reserve(initial_entries);
}

std::uint32_t LeafBucket::allocate(std::uint16_t capacity) {
    const auto offset = static_cast<std::uint32_t>(data_.size());
    data_.resize(data_.size() + capacity);
    return offset;
}

std::span<const BucketEntry> LeafBucket::view(std::uint32_t offset, std::uint16_t size) const {
    return {data_.data() + offset, size};
}

std::span<BucketEntry> LeafBucket::view(std::uint32_t offset, std::uint16_t size) {
    return {data_.data() + offset, size};
}

bool LeafBucket::push(std::uint32_t offset, std::uint16_t& size, std::uint16_t capacity, BucketEntry entry) {
    if (size == capacity)
        return false;
    data_[offset + size] = entry;
    ++size;
    return true;
}

void LeafBucket::clear() noexcept {
    data_.clear();
}

} // namespace copse::internal
