#pragma once

#include "topiary/impl/point_traits.hpp"

namespace topiary {

/// @brief Axis-aligned bounding box: per-axis inclusive lower and upper bounds.
/// @tparam Dim Spatial dimension.
template <int Dim>
struct BBox {
    detail::PointType<Dim> min_corner;
    detail::PointType<Dim> max_corner;
};

} // namespace topiary
