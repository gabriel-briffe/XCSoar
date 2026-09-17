// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeWorker.hpp"
#include "GlideConeContourWorker.hpp"
#include "GlideConeGpuWorker.hpp"
#include "GlideConeGridBuilder.hpp"
#include "GlideConeData.hpp"
#include "Geo/GeoPoint.hpp"
#include "util/Serial.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

struct GlideConeField;
class GlideConeOverlay;
class RasterTerrain;
class Waypoints;
struct ComputerSettings;
struct WaypointRendererSettings;
struct GlideConeSettings;

/**
 * Owns CPU grid, GPU propagate, and contour-build workers plus the
 * recompute / debounce state machine.  Does not draw.
 */
class GlideConeJobController {
  GlideConeWorker worker;
  GlideConeContourWorker contour_worker;
  GlideConeGpuWorker gpu_worker;

  std::uint64_t job_generation = 0;
  std::uint64_t contour_generation = 0;
  bool awaiting_grid = false;
  bool awaiting_gpu = false;
  bool awaiting_contours = false;

  GeoPoint computed_center = GeoPoint::Invalid();
  std::size_t computed_signature = 0;
  Serial computed_waypoint_serial{};
  bool computed_contours = false;
  bool computed_contour_polylines = true;

  /**
   * When true, next successful InstallField drops the geo label cache
   * (grid rebased).  Terrain/settings refreshes keep the same origin.
   */
  bool drop_labels_on_install = true;

  std::size_t debounce_signature = ~std::size_t{0};
  std::chrono::steady_clock::time_point debounce_since{};

  Serial debounce_waypoint_serial{};
  std::chrono::steady_clock::time_point waypoint_debounce_since{};

  /** Cooldown after starting/failing a job so cold-start DEM misses retry. */
  std::chrono::steady_clock::time_point last_job_attempt{};

public:
  [[gnu::pure]]
  bool IsBusy() const noexcept {
    return awaiting_grid || awaiting_gpu || awaiting_contours ||
      gpu_worker.IsBusy() ||
      const_cast<GlideConeContourWorker &>(contour_worker).IsBusy();
  }

  [[gnu::pure]]
  bool IsAwaitingGrid() const noexcept {
    return awaiting_grid;
  }

  [[gnu::pure]]
  bool IsAwaitingGpu() const noexcept {
    return awaiting_gpu;
  }

  [[gnu::pure]]
  bool IsAwaitingContours() const noexcept {
    return awaiting_contours;
  }

  [[gnu::pure]]
  bool HasContourState() const noexcept {
    return computed_contours || awaiting_contours;
  }

  /** Drop in-flight CPU/GPU/contour work. */
  void Abort(GlideConeOverlay &overlay) noexcept;

  void ClearFieldClaim(bool field_valid) noexcept;

  /**
   * Schedule grid/GPU work and install completed results into @p field.
   * Updates @p overlay when a new field is installed or contours arrive.
   *
   * @return false if the cone should clear (mode off / no GPU / no seed).
   */
  bool Update(GlideConeField &field, GlideConeOverlay &overlay,
              GeoPoint aircraft, bool aircraft_valid,
              GeoPoint target, bool target_valid,
              GeoPoint pending_seed, bool pending_valid,
              const ComputerSettings &settings,
              const RasterTerrain *terrain, const Waypoints *waypoints,
              const WaypointRendererSettings &waypoint_settings) noexcept;

  /**
   * Poll contour worker and request builds when needed.  Call from the
   * draw path when contours are enabled.
   */
  void UpdateContours(GlideConeField &field, GlideConeOverlay &overlay,
                      const GlideConeSettings &gc) noexcept;

  /** Cancel contours and clear field contour lines. */
  void ClearContours(GlideConeField &field,
                     GlideConeOverlay &overlay) noexcept;

private:
  void InstallField(GlideConeField &field, GlideConeOverlay &overlay,
                    GlideConePreparedGrid &&prepared,
                    GlideConeResult &&result) noexcept;

  void RequestContours(GlideConeField &field, bool polylines) noexcept;
};
