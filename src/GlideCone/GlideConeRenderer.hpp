// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeField.hpp"
#include "GlideConeJobController.hpp"
#include "GlideConeOverlay.hpp"
#include "Geo/GeoPoint.hpp"
#include "thread/Mutex.hxx"

#include <cstdint>
#include <functional>
#include <optional>

class Canvas;
class WindowProjection;
class RasterTerrain;
class Waypoints;
struct MapLook;
struct ComputerSettings;
struct WaypointRendererSettings;

/**
 * Map-facing glide-cone facade: pending single-mode target, last-good
 * field, job controller (CPU/GPU/contours), and overlay drawing.
 * Draw() never max-pools DEM and never runs GPU propagate.
 */
class GlideConeRenderer {
  Mutex mutex;

  /* shared pending target (guarded by #mutex; single-mode fallback) */
  GeoPoint pending_seed = GeoPoint::Invalid();
  double pending_seed_alt = 0;
  bool pending_valid = false;
  std::uint64_t pending_generation = 0;

  GlideConeField field;
  GlideConeJobController jobs;
  GlideConeOverlay overlay;

public:
  /**
   * Set the airport for which the glide cone should be computed (single
   * mode fallback).  May be called from any thread.
   */
  void SetTarget(GeoPoint seed, double elevation) noexcept;

  /** Forget the current target.  May be called from any thread. */
  void ClearTarget() noexcept;

  /**
   * Kick compute and draw the last-good path.  Draw thread; UI EGL
   * context must be current (shared GPU context is created from it).
   *
   * When @p pan_probe is valid (map pan / crosshair), also draws the
   * relay path from that location in the same style as the aircraft.
   */
  void Draw(Canvas &canvas, const WindowProjection &projection,
            GeoPoint aircraft, bool aircraft_valid,
            GeoPoint target, bool target_valid,
            const ComputerSettings &settings,
            RasterTerrain *terrain, const Waypoints *waypoints,
            const WaypointRendererSettings &waypoint_settings,
            const MapLook &look,
            GeoPoint pan_probe = GeoPoint::Invalid()) noexcept;

  /**
   * Required arrival altitude [m MSL] at @p location when the field is
   * valid and the cone is enabled; nullopt if unreachable / inactive.
   */
  [[gnu::pure]]
  std::optional<double>
  QueryRequiredAltitude(GeoPoint location) const noexcept;

  /**
   * True while CPU grid build, contour build, or GPU propagate is in
   * flight.
   */
  [[gnu::pure]]
  bool IsBusy() const noexcept {
    return jobs.IsBusy();
  }

  /**
   * Wake the map when a background job finishes (see
   * GlueMapWindow::InjectRedraw).  Call once during map setup.
   */
  void SetReadyCallback(std::function<void()> callback) noexcept {
    jobs.SetReadyCallback(std::move(callback));
  }

private:
  void DrawField(Canvas &canvas, const WindowProjection &projection,
                 GeoPoint aircraft, bool aircraft_valid,
                 GeoPoint pan_probe,
                 const ComputerSettings &settings,
                 const MapLook &look) noexcept;
};
