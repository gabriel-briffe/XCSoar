// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include <cstdint>
#include <vector>

/**
 * Input DEM grid for the glide cone GPU compute.
 *
 * The grid is a north-up regular lon/lat window centred on the "Goto"
 * airport.  Elevations are metres MSL (terrain plus ground clearance).
 */
struct GlideConeGrid {
  unsigned width = 0, height = 0;

  /** Approximate ground distance between adjacent cells [m]. */
  double cell_size_m = 0;

  /** Fixed glide ratio (L/D) used by the propagation. */
  double glide_ratio = 1;

  /** Sentinel "unreachable" altitude [m MSL]. */
  float max_alt = 0;

  /** Seed (airport) cell. */
  int home_x = 0, home_y = 0;

  /** Arrival altitude required over the seed cell [m MSL]. */
  float home_alt = 0;

  /** Upper bound on propagation iterations. */
  unsigned iteration_cap = 0;

  /** Terrain plus ground clearance [m MSL], size width*height. */
  std::vector<float> elevation;

  [[gnu::pure]]
  bool IsValid() const noexcept {
    return width > 0 && height > 0 &&
      elevation.size() == std::size_t(width) * height;
  }
};

/**
 * Output field produced by the glide cone GPU compute.  For each cell it
 * holds the minimum arrival altitude required to still glide to the seed
 * and the relay parent ("origin") cell used to trace the glide path.
 */
struct GlideConeResult {
  unsigned width = 0, height = 0;

  /** Required arrival altitude [m MSL]; >= max_alt means unreachable. */
  std::vector<float> altitudes;

  /** Relay parent cell coordinates (-1 = none). */
  std::vector<std::int32_t> origin_x;
  std::vector<std::int32_t> origin_y;

  [[gnu::pure]]
  bool IsValid() const noexcept {
    const std::size_t n = std::size_t(width) * height;
    return width > 0 && height > 0 &&
      altitudes.size() == n && origin_x.size() == n && origin_y.size() == n;
  }

  void Clear() noexcept {
    width = height = 0;
    altitudes.clear();
    origin_x.clear();
    origin_y.clear();
  }
};
