// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeField.hpp"
#include "GlideConeWorker.hpp"
#include "GlideConeContourWorker.hpp"
#include "GlideConeGpuWorker.hpp"
#include "Geo/GeoPoint.hpp"
#include "Math/Angle.hpp"
#include "ui/dim/Size.hpp"
#include "thread/Mutex.hxx"
#include "util/Serial.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class Canvas;
class WindowProjection;
class RasterTerrain;
class Waypoints;
struct MapLook;
struct ComputerSettings;
struct WaypointRendererSettings;
struct GlideConeSettings;

/**
 * Owns glide-cone CPU grid building (worker thread), GLES compute on a
 * dedicated shared-context thread, and drawing of the last-good relay
 * path.  Draw() never max-pools DEM and never runs GPU propagate.
 */
class GlideConeRenderer {
  Mutex mutex;

  /* shared pending target (guarded by #mutex; single-mode fallback) */
  GeoPoint pending_seed = GeoPoint::Invalid();
  double pending_seed_alt = 0;
  bool pending_valid = false;
  std::uint64_t pending_generation = 0;

  GlideConeWorker worker;
  GlideConeContourWorker contour_worker;
  GlideConeGpuWorker gpu_worker;
  std::uint64_t job_generation = 0;
  std::uint64_t contour_generation = 0;
  bool awaiting_grid = false;
  bool awaiting_gpu = false;
  bool awaiting_contours = false;

  /* last-good field, painted while a new job runs */
  GlideConeField field;
  GeoPoint computed_center = GeoPoint::Invalid();
  std::size_t computed_signature = 0;
  Serial computed_waypoint_serial{};
  bool computed_contours = false;
  bool computed_contour_polylines = true;

  /**
   * Contour labels anchored in geographic space.  Placement is redone
   * when zoom/settings change or the view drifts out of the 1.5× cache
   * (pan/rotate), after a settle debounce — not on every frame.
   */
  struct ContourLabel {
    GeoPoint location;
    /** Point ahead along the contour for screen-tangent orientation. */
    GeoPoint along;
    int level;
    PixelSize text_size;
    char text[32];
  };
  std::vector<ContourLabel> contour_labels;
  double label_cache_map_scale = -1;
  unsigned label_cache_spacing = 0;
  unsigned label_cache_font_h = 0;
  PixelSize label_cache_screen_size{};
  GeoPoint label_cache_center = GeoPoint::Invalid();
  Angle label_cache_angle = Angle::Zero();
  /** When true, next InstallField drops the geo label cache (grid rebased). */
  bool drop_labels_on_install = true;

  /**
   * Hold label rebuilds from the moment the compute center moves until
   * new contours are placed — covers the cooldown gap before awaiting_*.
   */
  bool labels_hold_rebase = false;

  /** Settle-debounce for label rebuilds (timer restarts while view moves). */
  bool label_rebuild_pending = false;
  std::chrono::steady_clock::time_point label_rebuild_since{};
  double label_rebuild_watch_scale = -1;
  GeoPoint label_rebuild_watch_center = GeoPoint::Invalid();
  Angle label_rebuild_watch_angle = Angle::Zero();
  /** Last logged debounce reason (avoid spamming identical lines). */
  const char *label_debounce_reason = nullptr;

  std::size_t debounce_signature = ~std::size_t{0};
  std::chrono::steady_clock::time_point debounce_since{};

  Serial debounce_waypoint_serial{};
  std::chrono::steady_clock::time_point waypoint_debounce_since{};

  /** Cooldown after starting/failing a job so cold-start DEM misses retry. */
  std::chrono::steady_clock::time_point last_job_attempt{};

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
            const RasterTerrain *terrain, const Waypoints *waypoints,
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
   * flight.  The map keeps redrawing so completed results are picked
   * up promptly.
   */
  [[gnu::pure]]
  bool IsBusy() const noexcept {
    return awaiting_grid || awaiting_gpu || awaiting_contours ||
      gpu_worker.IsBusy() ||
      const_cast<GlideConeContourWorker &>(contour_worker).IsBusy();
  }

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
  /** Drop in-flight CPU/GPU work. */
  void AbortJobs() noexcept;

  void InstallField(GlideConePreparedGrid &&prepared,
                    GlideConeResult &&result) noexcept;

  /** Undo computed_* claim so a failed cold-start build can retry. */
  void ClearJobClaim() noexcept;

  void RequestContours(bool polylines) noexcept;

  void InvalidateContourLabels() noexcept;

  void RebuildContourLabels(Canvas &canvas,
                            const WindowProjection &projection,
                            const GlideConeSettings &gc,
                            const MapLook &look) noexcept;

  void DrawContourLabels(Canvas &canvas,
                         const WindowProjection &projection,
                         const MapLook &look) const noexcept;

  void DrawTraceFrom(Canvas &canvas, const WindowProjection &projection,
                     GeoPoint from, const MapLook &look) const noexcept;

  void DrawField(Canvas &canvas, const WindowProjection &projection,
                 GeoPoint aircraft, bool aircraft_valid,
                 GeoPoint pan_probe,
                 const ComputerSettings &settings,
                 const MapLook &look) noexcept;
};
