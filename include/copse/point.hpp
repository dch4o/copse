// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
#pragma once

#include "copse/impl/point_traits.hpp"

namespace copse {

/// @brief Plain float point of fixed dimension; POD aggregate with `Dim` contiguous coordinates.
/// @tparam Dim Point dimensionality.
template <int Dim>
    requires detail::SupportedDim<Dim>
struct Point {
    float coords[Dim]; /// Contiguous per-axis coordinates.

    /// @brief Mutable access to axis `i`.
    /// @param i Axis index in `[0, Dim)`.
    /// @return Reference to coordinate `i`.
    float& operator[](int i) { return coords[i]; }

    /// @brief Read access to axis `i`.
    /// @param i Axis index in `[0, Dim)`.
    /// @return Value of coordinate `i`.
    float operator[](int i) const { return coords[i]; }

    /// @brief Element-wise equality across all axes.
    /// @param other Point to compare against.
    /// @return True iff every coordinate is bit-for-bit equal.
    bool operator==(const Point& other) const = default;

    /// @brief Squared Euclidean distance to `other`.
    /// @param other Point to measure against.
    /// @return Sum over axes of `(coords[i] - other.coords[i])^2`.
    float sq_dist(const Point& other) const {
        float result = 0.0f;
        for (int i = 0; i < Dim; ++i) {
            const float d = coords[i] - other.coords[i];
            result += d * d;
        }
        return result;
    }
};

} // namespace copse
