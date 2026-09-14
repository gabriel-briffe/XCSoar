// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeRenderer.hpp"
#include "GlideConeCompute.hpp"
#include "GlideConeLog.hpp"
#include "GlideConeStatus.hpp"
#include "Computer/Settings.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Terrain/Height.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Geo/GeoVector.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/dim/BulkPoint.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

/** Grid resolution (cells per side) of the compute window. */
static constexpr unsigned GLIDE_CONE_DIM = 256;

/** Hard cap on the compute window radius [m]. */
static constexpr double GLIDE_CONE_MAX_RADIUS_M = 200000;

/** Combined-mode window half-width factor (see gpu-MC AUTO_WINDOW_GLIDE_FACTOR). */
static constexpr double GLIDE_CONE_WINDOW_GLIDE_FACTOR = 1.25;

/** Combined-mode recompute threshold as a fraction of the window half-width
    (see gpu-MC AUTO_MAX_OFFSET_FROM_CENTER). */
static constexpr double GLIDE_CONE_MAX_OFFSET_FROM_CENTER = 0.25;

/** Single-mode recompute threshold when the target moves [m]. */
static constexpr double GLIDE_CONE_SEED_EPSILON_M = 50;

/** Debounce for parameter changes (see gpu-MC AUTO_COMPUTE_DEBOUNCE_MS). */
static constexpr std::chrono::milliseconds GLIDE_CONE_DEBOUNCE{400};

void
GlideConeRenderer::SetTarget(GeoPoint seed, double elevation) noexcept
{
  const std::lock_guard lock{mutex};
  pending_seed = seed;
  pending_seed_alt = elevation;
  pending_valid = seed.IsValid();
  ++pending_generation;

  GlideConeLog::Add("SetTarget: lat=%.5f lon=%.5f elev=%.0f valid=%d gen=%llu",
                    seed.latitude.Degrees(), seed.longitude.Degrees(),
                    elevation, int(seed.IsValid()),
                    (unsigned long long)pending_generation);
}

void
GlideConeRenderer::ClearTarget() noexcept
{
  const std::lock_guard lock{mutex};
  pending_valid = false;
  ++pending_generation;

  GlideConeLog::Add("ClearTarget gen=%llu",
                    (unsigned long long)pending_generation);
}

[[gnu::pure]]
static std::size_t
SettingsSignature(const GlideConeSettings &s) noexcept
{
  std::size_t h = std::hash<int>{}(int(s.mode));
  h = h * 31 + std::hash<double>{}(s.glide_ratio);
  h = h * 31 + std::hash<double>{}(s.max_altitude);
  h = h * 31 + std::hash<unsigned>{}(s.iteration_cap);
  return h;
}

bool
GlideConeRenderer::BuildField(GeoPoint center, double radius_m,
                              const std::vector<GeoPoint> &seeds,
                              const ComputerSettings &settings,
                              const RasterTerrain &terrain) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;

  const double ratio = std::clamp(gc.glide_ratio, 1.0, 200.0);
  const double max_alt = std::clamp(gc.max_altitude, 100.0, 10000.0);

  const GeoPoint north = GeoVector(radius_m, Angle::Zero()).EndPoint(center);
  const GeoPoint east =
    GeoVector(radius_m, Angle::QuarterCircle()).EndPoint(center);
  const GeoPoint south =
    GeoVector(radius_m, Angle::HalfCircle()).EndPoint(center);
  const GeoPoint west =
    GeoVector(radius_m, Angle::Degrees(270)).EndPoint(center);

  const GeoBounds bounds{GeoPoint(west.longitude, north.latitude),
                         GeoPoint(east.longitude, south.latitude)};
  if (!bounds.IsValid())
    return false;

  const unsigned dim = GLIDE_CONE_DIM;
  const double cell_size_m = (2 * radius_m) / dim;

  const double clearance = settings.task.route_planner.safety_height_terrain;
  const double arrival = settings.task.safety_height_arrival;
  const double invalid_elevation = max_alt + 10000;

  GlideConeGrid grid;
  grid.width = dim;
  grid.height = dim;
  grid.cell_size_m = cell_size_m;
  grid.glide_ratio = ratio;
  grid.max_alt = float(max_alt);
  grid.iteration_cap = gc.iteration_cap;
  grid.elevation.resize(std::size_t(dim) * dim);

  const Angle west_lng = bounds.GetWest();
  const Angle north_lat = bounds.GetNorth();
  const Angle span_lng = bounds.GetWidth();
  const Angle span_lat = bounds.GetHeight();

  for (unsigned j = 0; j < dim; ++j) {
    const Angle lat = north_lat - span_lat * ((j + 0.5) / dim);
    for (unsigned i = 0; i < dim; ++i) {
      const Angle lng = west_lng + span_lng * ((i + 0.5) / dim);
      const auto h = terrain.GetTerrainHeight(GeoPoint(lng, lat));
      const double t = h.ToDouble(invalid_elevation, 0.0);
      grid.elevation[std::size_t(j) * dim + i] = float(t + clearance);
    }
  }

  for (const GeoPoint &seed : seeds) {
    const double fx = (seed.longitude - west_lng).Native() / span_lng.Native();
    const double fy = (north_lat - seed.latitude).Native() / span_lat.Native();
    if (fx < 0 || fx >= 1 || fy < 0 || fy >= 1)
      continue;

    const int sx = std::clamp(int(fx * dim), 0, int(dim) - 1);
    const int sy = std::clamp(int(fy * dim), 0, int(dim) - 1);
    const double seed_terrain =
      terrain.GetTerrainHeight(seed).ToDouble(0.0, 0.0);
    grid.seeds.push_back({sx, sy, float(seed_terrain + arrival)});
  }

  if (grid.seeds.empty())
    return false;

  GlideConeResult result;
  if (!GlideConeCompute::Run(grid, result)) {
    GlideConeLog::Add("compute FAILED (see earlier lines for reason)");
    return false;
  }

  unsigned reachable = 0;
  const float max_alt_f = grid.max_alt;
  for (const float a : result.altitudes)
    if (a < max_alt_f)
      ++reachable;
  GlideConeLog::Add("compute ok: seeds=%u reachableCells=%u total=%u",
                    unsigned(grid.seeds.size()), reachable,
                    unsigned(result.altitudes.size()));

  field.result = std::move(result);
  field.bounds = bounds;
  field.cell_size_m = cell_size_m;
  field.max_alt = grid.max_alt;
  field.home_x = grid.seeds.front().x;
  field.home_y = grid.seeds.front().y;
  return field.IsValid();
}

void
GlideConeRenderer::Draw(Canvas &canvas, const WindowProjection &projection,
                        GeoPoint aircraft, bool aircraft_valid,
                        GeoPoint target, bool target_valid,
                        const ComputerSettings &settings,
                        const RasterTerrain *terrain,
                        const Waypoints *waypoints,
                        const MapLook &look) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;
  const auto mode = gc.mode;

  const std::size_t signature = SettingsSignature(gc);
  const bool diag = int(mode) != last_diag_mode ||
    signature != last_diag_signature;
  if (diag) {
    last_diag_mode = int(mode);
    last_diag_signature = signature;
    GlideConeLog::Add("Draw: mode=%d avail=%d terrain=%d aircraftValid=%d "
                      "targetValid=%d ratio=%.1f maxAlt=%.0f cap=%u",
                      int(mode), int(GlideConeCompute::Available()),
                      int(terrain != nullptr), int(aircraft_valid),
                      int(target_valid), gc.glide_ratio, gc.max_altitude,
                      gc.iteration_cap);
  }

  if (mode == GlideConeSettings::Mode::OFF || terrain == nullptr ||
      !GlideConeCompute::Available()) {
    GlideConeStatus::SetInvalid();
    return;
  }

  const double ratio = std::clamp(gc.glide_ratio, 1.0, 200.0);
  const double max_alt = std::clamp(gc.max_altitude, 100.0, 10000.0);

  /* determine the window centre and seeds for the selected mode */
  GeoPoint center = GeoPoint::Invalid();
  double radius_m = 0;
  std::vector<GeoPoint> seeds;
  double recompute_threshold_m = GLIDE_CONE_SEED_EPSILON_M;

  if (mode == GlideConeSettings::Mode::SINGLE) {
    GeoPoint seed;
    bool valid;
    if (target_valid) {
      seed = target;
      valid = true;
    } else {
      const std::lock_guard lock{mutex};
      seed = pending_seed;
      valid = pending_valid;
    }

    if (!valid) {
      field.Clear();
      have_field = false;
      computed_center = GeoPoint::Invalid();
      GlideConeStatus::SetInvalid();
      return;
    }

    center = seed;
    radius_m = std::min(max_alt * ratio, GLIDE_CONE_MAX_RADIUS_M);
    seeds.push_back(seed);
    recompute_threshold_m = GLIDE_CONE_SEED_EPSILON_M;
  } else { // COMBINED
    if (!aircraft_valid || waypoints == nullptr) {
      GlideConeStatus::SetInvalid();
      return;
    }

    center = aircraft;
    radius_m = std::min(GLIDE_CONE_WINDOW_GLIDE_FACTOR * max_alt * ratio,
                        GLIDE_CONE_MAX_RADIUS_M);
    recompute_threshold_m = GLIDE_CONE_MAX_OFFSET_FROM_CENTER * radius_m;
  }

  if (radius_m < 1000)
    radius_m = 1000;

  /* debounce parameter (glide ratio, etc.) changes */
  const auto now = std::chrono::steady_clock::now();
  const bool sig_changed = signature != computed_signature;
  if (sig_changed && signature != debounce_signature) {
    debounce_signature = signature;
    debounce_since = now;
  }
  const bool sig_ready = sig_changed &&
    now - debounce_since >= GLIDE_CONE_DEBOUNCE;

  const bool center_moved = !computed_center.IsValid() ||
    computed_center.DistanceS(center) > recompute_threshold_m;

  if (!have_field || center_moved || sig_ready) {
    /* single mode gathers its single seed above; combined gathered its
       seeds only when a recompute was due, so re-gather if needed */
    if (mode == GlideConeSettings::Mode::COMBINED && seeds.empty() &&
        waypoints != nullptr) {
      waypoints->VisitWithinRange(center, radius_m,
                                  [&seeds](const WaypointPtr &wp){
        if (wp->IsLandable())
          seeds.push_back(wp->location);
      });
    }

    have_field = !seeds.empty() &&
      BuildField(center, radius_m, seeds, settings, *terrain);
    computed_center = center;
    computed_signature = signature;
  }

  if (!have_field) {
    GlideConeStatus::SetInvalid();
    if (diag)
      GlideConeLog::Add("no path: field invalid / compute failed / no seeds");
    return;
  }

  if (!aircraft_valid) {
    GlideConeStatus::SetInvalid();
    return;
  }

  /* publish the required altitude at the aircraft for the InfoBox */
  {
    int ax, ay;
    if (field.GeoToCell(aircraft, ax, ay)) {
      const std::size_t index = std::size_t(ay) * field.result.width + ax;
      const float required = field.result.altitudes[index];
      if (required < field.max_alt)
        GlideConeStatus::Set({true, required});
      else
        GlideConeStatus::SetInvalid();
    } else {
      GlideConeStatus::SetInvalid();
    }
  }

  const std::vector<GeoPoint> path = field.Trace(aircraft);
  if (path.size() < 2)
    return;

  std::vector<BulkPixelPoint> points(path.size());
  std::transform(path.begin(), path.end(), points.begin(),
                 [&projection](const GeoPoint &p) {
                   return projection.GeoToScreen(p);
                 });

  canvas.Select(look.glide_cone_pen);
  canvas.DrawPolyline(points.data(), unsigned(points.size()));
}
