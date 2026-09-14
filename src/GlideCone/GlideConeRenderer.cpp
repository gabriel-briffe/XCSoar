// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeRenderer.hpp"
#include "GlideConeCompute.hpp"
#include "Computer/Settings.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Terrain/Height.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Geo/GeoVector.hpp"
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

void
GlideConeRenderer::SetTarget(GeoPoint seed, double elevation) noexcept
{
  const std::lock_guard lock{mutex};
  pending_seed = seed;
  pending_seed_alt = elevation;
  pending_valid = seed.IsValid();
  ++pending_generation;
}

void
GlideConeRenderer::ClearTarget() noexcept
{
  const std::lock_guard lock{mutex};
  pending_valid = false;
  ++pending_generation;
}

[[gnu::pure]]
static std::size_t
SettingsSignature(const GlideConeSettings &s) noexcept
{
  std::size_t h = std::hash<double>{}(s.glide_ratio);
  h = h * 31 + std::hash<double>{}(s.max_altitude);
  h = h * 31 + std::hash<unsigned>{}(s.iteration_cap);
  return h;
}

bool
GlideConeRenderer::BuildField(GeoPoint seed, double elevation,
                              const ComputerSettings &settings,
                              const RasterTerrain &terrain) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;

  const double ratio = std::clamp(gc.glide_ratio, 1.0, 200.0);
  const double max_alt = std::clamp(gc.max_altitude, 100.0, 10000.0);
  double radius_m = std::min(max_alt * ratio, GLIDE_CONE_MAX_RADIUS_M);
  if (radius_m < 1000)
    radius_m = 1000;

  const GeoPoint north = GeoVector(radius_m, Angle::Zero()).EndPoint(seed);
  const GeoPoint east =
    GeoVector(radius_m, Angle::QuarterCircle()).EndPoint(seed);
  const GeoPoint south =
    GeoVector(radius_m, Angle::HalfCircle()).EndPoint(seed);
  const GeoPoint west =
    GeoVector(radius_m, Angle::Degrees(270)).EndPoint(seed);

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

  const double seed_terrain =
    terrain.GetTerrainHeight(seed).ToDouble(elevation, 0.0);
  grid.home_alt = float(seed_terrain + arrival);

  int home_x = int(((seed.longitude - west_lng).Native() /
                    span_lng.Native()) * dim);
  int home_y = int(((north_lat - seed.latitude).Native() /
                    span_lat.Native()) * dim);
  grid.home_x = std::clamp(home_x, 0, int(dim) - 1);
  grid.home_y = std::clamp(home_y, 0, int(dim) - 1);

  GlideConeResult result;
  if (!GlideConeCompute::Run(grid, result))
    return false;

  field.result = std::move(result);
  field.bounds = bounds;
  field.cell_size_m = cell_size_m;
  field.max_alt = grid.max_alt;
  field.home_x = grid.home_x;
  field.home_y = grid.home_y;
  return field.IsValid();
}

void
GlideConeRenderer::Draw(Canvas &canvas, const WindowProjection &projection,
                        GeoPoint aircraft, bool aircraft_valid,
                        const ComputerSettings &settings,
                        const RasterTerrain *terrain,
                        const MapLook &look) noexcept
{
  if (!settings.glide_cone.enabled || terrain == nullptr ||
      !GlideConeCompute::Available())
    return;

  GeoPoint seed;
  double seed_alt;
  bool valid;
  std::uint64_t generation;

  {
    const std::lock_guard lock{mutex};
    seed = pending_seed;
    seed_alt = pending_seed_alt;
    valid = pending_valid;
    generation = pending_generation;
  }

  if (!valid) {
    field.Clear();
    have_field = false;
    return;
  }

  const std::size_t signature = SettingsSignature(settings.glide_cone);
  if (!have_field || generation != computed_generation ||
      signature != computed_signature) {
    have_field = BuildField(seed, seed_alt, settings, *terrain);
    computed_generation = generation;
    computed_signature = signature;
  }

  if (!have_field || !aircraft_valid)
    return;

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
