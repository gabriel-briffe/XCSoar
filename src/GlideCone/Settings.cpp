// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Settings.hpp"

#include <algorithm>

void
GlideConeSettings::SetDefaults() noexcept
{
  mode = Mode::OFF;
  glide_ratio = 25;
  max_altitude = 3000;
  cell_size = DEFAULT_CELL_SIZE_M;
  iteration_cap = 2000;
  contours = false;
  contour_polylines = true;
  contours_min_scale = 30000;
  label_spacing = 40;
}

double
GlideConeSettings::WindowRadiusM() const noexcept
{
  const double ratio = std::clamp(glide_ratio, 1.0, 200.0);
  const double max_alt = std::clamp(max_altitude, 100.0, 10000.0);
  const double factor = mode == Mode::COMBINED
    ? COMBINED_WINDOW_FACTOR
    : SINGLE_WINDOW_FACTOR;
  return std::clamp(factor * max_alt * ratio,
                    MIN_WINDOW_RADIUS_M, MAX_WINDOW_RADIUS_M);
}
