#pragma once

#include "copse/point.hpp"

#include <algorithm>
#include <cmath>

namespace copse {

/// @brief Axis-aligned bounding box: per-axis inclusive lower and upper bounds.
/// @tparam Dim Spatial dimension.
template <int Dim>
struct BBox {
    Point<Dim> min_corner; /// Per-axis lower bounds (inclusive).
    Point<Dim> max_corner; /// Per-axis upper bounds (inclusive).

    /// @brief Grow the box to cover `point`: per-axis min/max against the corners.
    /// @param point Point to absorb.
    void expand(const Point<Dim>& point) {
        for (int i = 0; i < Dim; ++i) {
            min_corner[i] = std::min(min_corner[i], point[i]);
            max_corner[i] = std::max(max_corner[i], point[i]);
        }
    }

    /// @brief Inclusive containment of a point on every axis.
    /// @param point Point to test.
    /// @return True iff `min_corner[i] <= point[i] <= max_corner[i]` for all axes.
    bool contains(const Point<Dim>& point) const {
        for (int i = 0; i < Dim; ++i) {
            if (point[i] < min_corner[i] || point[i] > max_corner[i]) {
                return false;
            }
        }
        return true;
    }

    /// @brief Whether `other` lies fully inside this box (inclusive on every axis).
    /// @param other Box to test for containment.
    /// @return True iff `other` is entirely within this box.
    bool contains(const BBox& other) const {
        for (int i = 0; i < Dim; ++i) {
            if (other.min_corner[i] < min_corner[i] || other.max_corner[i] > max_corner[i]) {
                return false;
            }
        }
        return true;
    }

    /// @brief Whether this box and `other` fail to overlap on some axis.
    /// @param other Box to test against.
    /// @return True iff the boxes are separated on at least one axis.
    bool disjoint(const BBox& other) const {
        for (int i = 0; i < Dim; ++i) {
            if (max_corner[i] < other.min_corner[i] || min_corner[i] > other.max_corner[i]) {
                return true;
            }
        }
        return false;
    }

    /// @brief Squared distance from `point` to this box; 0 if `point` is inside.
    /// @param point Query point.
    /// @return Squared Euclidean distance to the nearest box surface, 0 when inside.
    float min_sq_dist(const Point<Dim>& point) const {
        float result = 0.0f;
        for (int i = 0; i < Dim; ++i) {
            const float below = std::max(min_corner[i] - point[i], 0.0f);
            const float above = std::max(point[i] - max_corner[i], 0.0f);
            result += below * below + above * above;
        }
        return result;
    }

    /// @brief Squared distance from `point` to the farthest corner of this box.
    /// @param point Query point.
    /// @return Squared Euclidean distance to the farthest box corner.
    float max_sq_dist(const Point<Dim>& point) const {
        float result = 0.0f;
        for (int i = 0; i < Dim; ++i) {
            const float to_min  = std::abs(point[i] - min_corner[i]);
            const float to_max  = std::abs(point[i] - max_corner[i]);
            const float farthest = std::max(to_min, to_max);
            result += farthest * farthest;
        }
        return result;
    }
};

} // namespace copse
