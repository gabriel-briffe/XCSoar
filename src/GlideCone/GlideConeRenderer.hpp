// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeField.hpp"
#include "GlideConeCompute.hpp"
#include "GlideConeWorker.hpp"
#include "Geo/GeoPoint.hpp"
#include "thread/Mutex.hxx"
#include "util/Serial.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class Canvas;
class WindowProjection;
class RasterTerrain;
class Waypoints;
struct MapLook;
struct ComputerSettings;
struct WaypointRendererSettings;

/**
 * Owns glide-cone CPU grid building (worker thread), time-sliced GPU
 * propagate (draw thread), and drawing of the last-good relay path.
 *
 * Draw() never max-pools DEM, never walks waypoints, and never waits
 * for the full GPU iteration cap.
 */
class GlideConeRenderer {
  Mutex mutex;

  /* shared pending target (guarded by #mutex; single-mode fallback) */
  GeoPoint pending_seed = GeoPoint::Invalid();
  double pending_seed_alt = 0;
  bool pending_valid = false;
  std::uint64_t pending_generation = 0;

  GlideConeWorker worker;
  GlideConeGpuSession gpu;
  std::unique_ptr<GlideConePreparedGrid> gpu_input;
  std::uint64_t job_generation = 0;
  bool awaiting_grid = false;

  /* last-good field, painted while a new job runs */
  GlideConeField field;
  GeoPoint computed_center = GeoPoint::Invalid();
  std::size_t computed_signature = 0;
  Serial computed_terrain_serial{};
  Serial computed_waypoint_serial{};
  bool computed_contours = false;

  std::size_t debounce_signature = ~std::size_t{0};
  std::chrono::steady_clock::time_point debounce_since{};

  Serial debounce_terrain_serial{};
  std::chrono::steady_clock::time_point terrain_debounce_since{};
  Serial debounce_waypoint_serial{};
  std::chrono::steady_clock::time_point waypoint_debounce_since{};

public:
  /**
   * Set the airport for which the glide cone should be computed (single
   * mode fallback).  May be called from any thread.
   */
  void SetTarget(GeoPoint seed, double elevation) noexcept;

  /** Forget the current target.  May be called from any thread. */
  void ClearTarget() noexcept;

  /**
   * Kick or step compute and draw the last-good path.  Draw thread,
   * OpenGL context current.
   */
  void Draw(Canvas &canvas, const WindowProjection &projection,
            GeoPoint aircraft, bool aircraft_valid,
            GeoPoint target, bool target_valid,
            const ComputerSettings &settings,
            const RasterTerrain *terrain, const Waypoints *waypoints,
            const WaypointRendererSettings &waypoint_settings,
            const MapLook &look) noexcept;

  /**
   * Expand a map-view terrain request so it also covers the glide-cone
   * compute window (seed in single mode, aircraft in combined).
   */
  static void AdjustTerrainCoverage(const ComputerSettings &settings,
                                    GeoPoint aircraft, bool aircraft_valid,
                                    GeoPoint target, bool target_valid,
                                    GeoPoint &location,
                                    double &radius) noexcept;

private:
  /** Drop in-flight CPU/GPU work.  GL context must be current. */
  void AbortJobs() noexcept;

  void InstallField(GlideConePreparedGrid &&prepared,
                    GlideConeResult &&result) noexcept;

  void DrawField(Canvas &canvas, const WindowProjection &projection,
                 GeoPoint aircraft, bool aircraft_valid,
                 const ComputerSettings &settings,
                 const MapLook &look) noexcept;
};
