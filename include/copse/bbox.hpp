#pragma once

#include "copse/impl/point_traits.hpp"

namespace copse {

/// @brief Axis-aligned bounding box: per-axis inclusive lower and upper bounds.
/// @tparam Dim Spatial dimension.
template <int Dim>
struct BBox {
    detail::PointType<Dim> min_corner; /// Per-axis lower bounds (inclusive).
    detail::PointType<Dim> max_corner; /// Per-axis upper bounds (inclusive).
};

} // namespace copse
