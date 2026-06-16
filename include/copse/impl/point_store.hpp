// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
#pragma once

#include "copse/point.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace copse::internal {

/// @brief Backing store for points, liveness, and FIFO buffer bookkeeping.
/// @tparam Dim Point dimensionality.
template <int Dim>
class PointStore {
public:
    using Point = copse::Point<Dim>;

    /// @brief Construct with fixed capacity (must be > 0; validated by enclosing KDTreeImpl).
    /// @param capacity Fixed slot capacity.
    explicit PointStore(std::size_t capacity);

    /// @brief Fixed capacity supplied at construction.
    /// @return The configured capacity.
    std::size_t capacity() const noexcept;

    /// @brief Live point count.
    /// @return Number of live points currently stored.
    std::size_t size() const noexcept;

    /// @brief True iff `index` (in `[0, capacity)`) currently holds a live point.
    /// @param index Slot index to test.
    /// @return True iff the slot holds a live point.
    bool is_live(std::uint32_t index) const noexcept;

    /// @brief Read access to a stored point; `index` must be live.
    /// @param index Live slot index.
    /// @return Reference to the stored point.
    const Point& point(std::uint32_t index) const noexcept;

    /// @brief Current generation stamp for `index`.
    /// @param index Slot index to read.
    /// @return The slot's current generation stamp.
    std::uint32_t generation(std::uint32_t index) const noexcept;

    /// @brief Acquire an index (free or FIFO-evicted), write the point, return the assigned index.
    /// @param point Point to store.
    /// @return The assigned slot index.
    std::uint32_t acquire(const Point& point);

    /// @brief Free a live index (explicit remove).
    /// @param index Slot index to free.
    void release(std::uint32_t index);

    /// @brief Iterate over all live (index, point) pairs.
    /// @param fn Callback invoked with each live (index, point) pair.
    void for_each_live(std::function<void(std::uint32_t, const Point&)> fn) const;

private:
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

} // namespace copse::internal
