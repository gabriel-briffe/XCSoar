// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeGridBuilder.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Terrain/RasterMap.hpp"
#include "Terrain/RasterProjection.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Engine/Waypoint/Waypoint.hpp"

#include <algorithm>
#include <cmath>

bool
BuildGlideConeGrid(const GlideConeGridRequest &request,
                   const RasterTerrain &terrain,
                   const Waypoints *waypoints,
                   GlideConePreparedGrid &out) noexcept
{
  if (!request.center.IsValid() || request.radius_m <= 0)
    return false;

  const double ratio = std::clamp(request.glide_ratio, 1.0, 200.0);
  const double max_alt = std::clamp(request.max_altitude, 100.0, 10000.0);
  const double desired_cell = std::clamp(request.cell_size, 50.0, 5000.0);
  const double radius_m = request.radius_m;

  const RasterTerrain::Lease lease{terrain};
  const RasterMap &map = lease;
  if (!map.IsDefined())
    return false;

  const RasterProjection &proj = map.GetProjection();

  double dem_x = map.PixelDistanceX(request.center, 1);
  double dem_y = map.PixelDistanceY(request.center, 1);
  if (dem_x < 1)
    dem_x = 1;
  if (dem_y < 1)
    dem_y = 1;

  const double dem_ref = std::sqrt(dem_x * dem_y);
  unsigned pool = std::max(1u,
                           (unsigned)std::lround(desired_cell / dem_ref));

  double cell_x = pool * dem_x;
  double cell_y = pool * dem_y;

  unsigned half_x = std::max(1u, (unsigned)std::lround(radius_m / cell_x));
  unsigned half_y = std::max(1u, (unsigned)std::lround(radius_m / cell_y));

  if (half_x > GLIDE_CONE_MAX_DIM / 2 || half_y > GLIDE_CONE_MAX_DIM / 2) {
    const unsigned need_pool_x = std::max(1u, (unsigned)std::lround(
      (radius_m / double(GLIDE_CONE_MAX_DIM / 2)) / dem_x));
    const unsigned need_pool_y = std::max(1u, (unsigned)std::lround(
      (radius_m / double(GLIDE_CONE_MAX_DIM / 2)) / dem_y));
    pool = std::max(pool, std::max(need_pool_x, need_pool_y));
    cell_x = pool * dem_x;
    cell_y = pool * dem_y;
    half_x = std::min(GLIDE_CONE_MAX_DIM / 2,
                      std::max(1u, (unsigned)std::lround(radius_m / cell_x)));
    half_y = std::min(GLIDE_CONE_MAX_DIM / 2,
                      std::max(1u, (unsigned)std::lround(radius_m / cell_y)));
  }

  const unsigned dim_x = 2 * half_x;
  const unsigned dim_y = 2 * half_y;

  const auto c = proj.ProjectCoarse(request.center);
  const SignedRasterLocation origin{
    c.x - int(half_x * pool),
    c.y - int(half_y * pool),
  };

  const auto nw = proj.UnprojectCoarse(origin);
  const auto se = proj.UnprojectCoarse(SignedRasterLocation{
    origin.x + int(dim_x * pool),
    origin.y + int(dim_y * pool),
  });
  const GeoBounds bounds{nw, se};
  if (!bounds.IsValid())
    return false;

  const float invalid_elevation = float(max_alt + 10000);

  GlideConeGrid grid;
  grid.width = dim_x;
  grid.height = dim_y;
  grid.cell_size_x_m = cell_x;
  grid.cell_size_y_m = cell_y;
  grid.glide_ratio = ratio;
  grid.max_alt = float(max_alt);
  grid.iteration_cap = request.iteration_cap;
  grid.elevation.resize(std::size_t(dim_x) * dim_y);

  std::vector<GeoPoint> seeds = request.seeds;
  if (request.combined) {
    seeds.clear();
    if (waypoints != nullptr)
      waypoints->VisitWithinRange(request.center, radius_m,
        [&seeds, &request](const WaypointPtr &wp) {
          if (wp->IsLandable() &&
              request.waypoint_settings.IsWaypointDisplayed(*wp))
            seeds.push_back(wp->location);
        });
    if (seeds.empty())
      return false;
  } else if (seeds.empty()) {
    return false;
  }

  map.MaxPoolElevation(origin, pool, dim_x, dim_y,
                       grid.elevation.data(), invalid_elevation);
  for (float &e : grid.elevation)
    if (e < invalid_elevation)
      e += float(request.clearance);

  for (const GeoPoint &seed : seeds) {
    const auto sp = proj.ProjectCoarse(seed);
    const int sx = (sp.x - origin.x) / int(pool);
    const int sy = (sp.y - origin.y) / int(pool);
    if (sx < 0 || sy < 0 ||
        unsigned(sx) >= dim_x || unsigned(sy) >= dim_y)
      continue;

    const double seed_terrain = map.GetHeight(seed).ToDouble(0.0, 0.0);
    grid.seeds.push_back({sx, sy, float(seed_terrain + request.arrival)});
  }

  if (grid.seeds.empty())
    return false;

  out.generation = request.generation;
  out.center = request.center;
  out.bounds = bounds;
  out.grid = std::move(grid);
  return true;
}
