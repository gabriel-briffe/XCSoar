// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Settings for the terrain-aware glide cone overlay.
 *
 * The glide cone is a GPU (OpenGL ES 3.1 compute) reachability field
 * computed around a "Goto" airport; the resulting relay path from the
 * aircraft back to the airport is drawn on the map.  The computation is
 * only available on targets that provide a GLES 3.1 compute context
 * (currently Android); on other targets these settings are inert.
 */
struct GlideConeSettings {
  /** Draw the glide cone path for the current "Goto" airport. */
  bool enabled;

  /**
   * Fixed glide ratio (L/D) used for the cone propagation: horizontal
   * metres travelled per metre of altitude lost.
   */
  double glide_ratio;

  /**
   * Maximum working altitude [m MSL].  Also bounds the size of the
   * computation window (radius = max_altitude * glide_ratio).
   */
  double max_altitude;

  /** Upper bound on propagation iterations (0 = use internal default). */
  unsigned iteration_cap;

  void SetDefaults() noexcept;
};
