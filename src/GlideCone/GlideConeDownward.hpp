// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"
#include "Settings.hpp"

#include <cstdint>
#include <functional>
#include <vector>

/**
 * One downward optional-area pass (one L/D).  Floors are the cone
 * altitudes the shader treats as the ground it must not cross.
 */
struct GlideConeDownwardPass {
  double ratio = 1;
  int gi = -1;
  int gj = -1;
  float start_alt = 0;
  std::vector<float> floors;
};

/**
 * CPU-side request for the GLES downward shader.  The compute thread
 * owns the run; the draw thread only fills this and later reads
 * #GlideConeDownwardReady.
 */
struct GlideConeDownwardJob {
  std::uint64_t generation = 0;
  unsigned width = 0;
  unsigned height = 0;
  double cell_x = 0;
  double cell_y = 0;
  float max_alt = 0;
  GeoBounds bounds = GeoBounds::Invalid();
  GeoPoint start_location = GeoPoint::Invalid();
  /** Glider cell the seed was resolved from. */
  int aircraft_gi = -1;
  int aircraft_gj = -1;
  /**
   * Relay-path ground distance [m] glider → airport (InfoBox path).
   * Used with #margin_m to pick the first convergence check.
   */
  double path_distance_m = 0;
  /**
   * Height above the cone [m]: nav altitude − proof altitude, floored
   * at 0.  Half of this × L/D is the no-terrain reach beyond the path.
   */
  double margin_m = 0;
  /** Same setting as the upward cone (#GlideConeSettings::iteration_cap). */
  unsigned iteration_cap = 2000;
  GlideConeSettings::OptionsDisplay display =
    GlideConeSettings::OptionsDisplay::MARGIN;
  /**
   * Pass 0 is the full L/D field.  Degraded mode adds 90% then 80%.
   * Passes run one after another on the reused buffer pool: a single
   * GLES queue cannot overlap them the way WebGPU Promise.all can.
   */
  std::vector<GlideConeDownwardPass> passes;
};

struct GlideConeDownwardReady {
  std::uint64_t generation = 0;
  bool ok = false;
  /** True when a pass hit #GlideConeDownwardJob::iteration_cap. */
  bool hit_iteration_cap = false;
  unsigned iterations = 0;
  unsigned width = 0;
  unsigned height = 0;
  GeoBounds bounds = GeoBounds::Invalid();
  GeoPoint start_location = GeoPoint::Invalid();
  int aircraft_gi = -1;
  int aircraft_gj = -1;
  int start_index = -1;
  GlideConeSettings::OptionsDisplay display =
    GlideConeSettings::OptionsDisplay::MARGIN;
  std::vector<float> floors;
  std::vector<float> arrival;
  std::vector<int> origin;
  std::vector<std::uint8_t> mask;
  std::vector<std::uint8_t> mask10;
  std::vector<std::uint8_t> mask20;
};

#ifdef HAVE_GLES_COMPUTE

/**
 * Run @p job on the current GLES 3.1 context.  Closest match to
 * gpu-MC computeDownward: propagate without atomics, a separate
 * change-sum pass: first check from options reach (path + half margin
 * × L/D in cells, capped by half the grid); if still changing, wait
 * half that reach again; staging-buffer readback.  Buffer pool is
 * reused for the same cell count.
 */
bool
RunGlideConeDownward(const GlideConeDownwardJob &job,
                     const std::function<bool()> &should_abort,
                     GlideConeDownwardReady &out) noexcept;

/** Drop the buffer pool.  The compute context must be current. */
void
DestroyGlideConeDownward() noexcept;

#endif
