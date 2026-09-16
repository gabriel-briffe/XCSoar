// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include <cstdint>

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
  enum class Mode : uint8_t {
    /** Feature disabled. */
    OFF,

    /** Single seed: the current "Goto" airport. */
    SINGLE,

    /**
     * Combined multi-seed mode: all landables within a moving window
     * around the aircraft, recomputed as the aircraft moves.
     */
    COMBINED,
  };

  /** Glide cone mode. */
  Mode mode;

  /**
   * Fixed glide ratio (L/D) used for the cone propagation: horizontal
   * metres travelled per metre of altitude lost.
   */
  double glide_ratio;

  /**
   * Maximum working altitude [m MSL].  Also bounds the size of the
   * computation window (see WindowRadiusM()).
   */
  double max_altitude;

  /**
   * Target ground size of one GPU grid cell [m].  Native DEM pixels
   * are max-pooled to this step (snapped to an integer pool factor).
   * East–west and north–south metres per cell are derived separately
   * from the DEM projection so reach contours stay circular.
   */
  double cell_size;

  /** Upper bound on propagation iterations (0 = use internal default). */
  unsigned iteration_cap;

  /** Draw 100 m altitude contour lines of the reachability field. */
  bool contours;

  /**
   * Only show contours and labels when the map scale (see
   * WindowProjection::GetMapScale(), metres) is at most this value, i.e.
   * when zoomed in far enough.
   */
  double contours_min_scale;

  /**
   * Minimum screen distance between labels of the same altitude, as a
   * percentage of min(screen width, screen height).  Valid range 20–100.
   * Different altitudes only need non-overlapping text.
   */
  unsigned label_spacing;

  /** Half-width factor for single-mode (around the seed). */
  static constexpr double SINGLE_WINDOW_FACTOR = 1.1;

  /** Half-width factor for combined-mode (around the aircraft). */
  static constexpr double COMBINED_WINDOW_FACTOR = 1.25;

  static constexpr double MAX_WINDOW_RADIUS_M = 200000;
  static constexpr double MIN_WINDOW_RADIUS_M = 1000;
  static constexpr double DEFAULT_CELL_SIZE_M = 400;

  void SetDefaults() noexcept;

  [[gnu::pure]]
  constexpr bool IsEnabled() const noexcept {
    return mode != Mode::OFF;
  }

  /**
   * Compute-window half-width [m]: 1.1 × max_alt × L/D in single mode,
   * 1.25 × in combined mode, clamped to MIN/MAX_WINDOW_RADIUS_M.
   */
  [[gnu::pure]]
  double WindowRadiusM() const noexcept;
};
