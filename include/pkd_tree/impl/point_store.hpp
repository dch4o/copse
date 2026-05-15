#pragma once

#include "pkd_tree/impl/point_traits.hpp"

#include <Eigen/Core>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace pkd_tree::internal {

/// @brief Backing store for points, liveness, and FIFO buffer bookkeeping.
template <int Dim>
class PointStore {
public:
    using Point = detail::PointType<Dim>;

    /// @brief Construct with fixed capacity (must be > 0; validated by enclosing FixedKdTreeImpl).
    explicit PointStore(std::size_t capacity);

    std::size_t capacity() const noexcept;

    std::size_t size() const noexcept;

    /// @brief True iff `index` (in `[0, capacity)`) currently holds a live point.
    bool is_live(std::uint32_t index) const noexcept;

    /// @brief Read access to a stored point; `index` must be live.
    const Point& point(std::uint32_t index) const noexcept;

    /// @brief Current generation stamp for `index`; bumped on every reuse (release or FIFO evict).
    std::uint32_t generation(std::uint32_t index) const noexcept;

    /// @brief Acquire an index (free or FIFO-evicted), write the point, return the assigned index.
    std::uint32_t acquire(const Point& point);

    /// @brief Free a live index (explicit remove).
    void release(std::uint32_t index);

    /// @brief Iterate over all live (index, point) pairs.
    void for_each_live(std::function<void(std::uint32_t, const Point&)> fn) const;

private:
    static constexpr std::uint32_t INVALID_POS = ~std::uint32_t{0}; /// Sentinel for unused buf_pos_.

    std::size_t capacity_ = 0;
    std::size_t live_     = 0;

    std::vector<Point>         points_;
    std::vector<std::uint8_t>  live_bits_;
    std::vector<std::uint32_t> generations_; /// Per-slot reuse counter; stamped into bucket entries.

    std::vector<std::uint32_t> buffer_;  /// FIFO buffer of occupied indices in arrival order.
    std::vector<std::uint32_t> buf_pos_; /// Index -> buffer position, enabling O(1) release().
    std::uint32_t              buf_head_ = 0;
    std::uint32_t              buf_tail_ = 0;
};

} // namespace pkd_tree::internal
