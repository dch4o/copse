#include "topiary/impl/point_store.hpp"

#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace topiary::internal {

namespace {
/// Sentinel marking an unused `buf_pos_` slot.
constexpr std::uint32_t INVALID_POS = ~std::uint32_t{0};
} // namespace

template <int Dim>
PointStore<Dim>::PointStore(std::size_t capacity)
    : capacity_(capacity)
    , points_(capacity)
    , live_bits_(capacity, 0)
    , generations_(capacity, 0)
    , buffer_(capacity)
    , buf_pos_(capacity, INVALID_POS) {
    if (capacity == 0) {
        throw std::invalid_argument{"PointStore: capacity must be > 0"};
    }
    std::iota(buffer_.begin(), buffer_.end(), std::uint32_t{0});
}

template <int Dim>
std::size_t PointStore<Dim>::capacity() const noexcept {
    return capacity_;
}

template <int Dim>
std::size_t PointStore<Dim>::size() const noexcept {
    return live_;
}

template <int Dim>
bool PointStore<Dim>::is_live(std::uint32_t index) const noexcept {
    return index < capacity_ && live_bits_[index] != 0;
}

template <int Dim>
const typename PointStore<Dim>::Point& PointStore<Dim>::point(std::uint32_t index) const noexcept {
    return points_[index];
}

template <int Dim>
std::uint32_t PointStore<Dim>::generation(std::uint32_t index) const noexcept {
    return generations_[index];
}

template <int Dim>
std::uint32_t PointStore<Dim>::acquire(const Point& point) {
    if (live_ == capacity_) {
        const std::uint32_t victim = buffer_[buf_head_];
        ++generations_[victim];
        live_bits_[victim] = 0;
        buf_pos_[victim]   = INVALID_POS;
        buf_head_          = (buf_head_ + 1) % capacity_;
        --live_;
    }
    const std::uint32_t index = buffer_[buf_tail_];
    ++generations_[index];
    points_[index]    = point;
    live_bits_[index] = 1;
    buf_pos_[index]   = buf_tail_;
    buf_tail_         = (buf_tail_ + 1) % capacity_;
    ++live_;
    return index;
}

template <int Dim>
void PointStore<Dim>::release(std::uint32_t index) {
    const std::uint32_t pos      = buf_pos_[index];
    const std::uint32_t last_pos = (buf_tail_ + capacity_ - 1) % capacity_;
    if (pos != last_pos) {
        const std::uint32_t moved = buffer_[last_pos];
        buffer_[pos]              = moved;
        buf_pos_[moved]           = pos;
    }
    buffer_[last_pos] = index;
    buf_pos_[index]   = INVALID_POS;
    live_bits_[index] = 0;
    buf_tail_         = last_pos;
    --live_;
}

template <int Dim>
void PointStore<Dim>::for_each_live(std::function<void(std::uint32_t, const Point&)> fn) const {
    if (live_ == 0) {
        return;
    }
    std::size_t pos = buf_head_;
    for (std::size_t i = 0; i < live_; ++i) {
        const std::uint32_t index = buffer_[pos];
        fn(index, points_[index]);
        pos = (pos + 1) % capacity_;
    }
}

template class PointStore<2>;
template class PointStore<3>;
template class PointStore<4>;

} // namespace topiary::internal
