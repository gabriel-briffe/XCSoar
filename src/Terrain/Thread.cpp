// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Thread.hpp"
#include "RasterTerrain.hpp"
#include "Projection/WindowProjection.hpp"
#include "thread/Util.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

TerrainThread::TerrainThread(RasterTerrain &_terrain,
                             std::function<void()> &&_callback)
  :StandbyThread("Terrain"), terrain(_terrain),
   callback(std::move(_callback)) {}

void
TerrainThread::Trigger(const WindowProjection &projection,
                       double dem_medium_scale,
                       double dem_coarse_scale)
{
  assert(projection.IsValid());

  const std::lock_guard lock{mutex};

  GeoPoint center = projection.GetGeoScreenCenter();
  auto radius = projection.GetScreenHalfDiagonalMeters();
  const auto screen_w = std::max(1, (int)projection.GetScreenSize().width);
  const double scale_bar = projection.GetScreenWidthMeters();
  const double mpp = scale_bar / double(screen_w);

  /* Skip when we already cover this view.  Still run when zooming in
     (smaller mpp) so display LOD can step up to medium/fine. */
  if (last_center.IsValid() && last_radius >= radius &&
      last_meters_per_pixel > 0 &&
      last_meters_per_pixel <= mpp * 1.05 &&
      std::fabs(last_scale_bar - scale_bar) <= scale_bar * 0.05 &&
      last_dem_medium_scale == dem_medium_scale &&
      last_dem_coarse_scale == dem_coarse_scale &&
      last_center.DistanceS(center) < 1000)
    return;

  next_center = center;
  next_radius = radius;
  next_meters_per_pixel = mpp;
  next_scale_bar = scale_bar;
  next_dem_medium_scale = dem_medium_scale;
  next_dem_coarse_scale = dem_coarse_scale;
  StandbyThread::Trigger();
}

void
TerrainThread::Tick() noexcept
{
  SetIdlePriority(); // TODO: call only once

  bool again = true;
  while (next_center.IsValid() && again && !IsStopped()) {
    const GeoPoint center = next_center;
    const auto radius = next_radius;
    const double mpp = next_meters_per_pixel;
    const double scale_bar = next_scale_bar;
    const double dem_medium = next_dem_medium_scale;
    const double dem_coarse = next_dem_coarse_scale;

    {
      const ScopeUnlock unlock(mutex);
      again = terrain.UpdateTiles(center, radius, mpp, scale_bar,
                                  dem_medium, dem_coarse);
    }

    last_center = center;
    last_radius = radius;
    last_meters_per_pixel = mpp;
    last_scale_bar = scale_bar;
    last_dem_medium_scale = dem_medium;
    last_dem_coarse_scale = dem_coarse;
  }

  /* notify the client */
  if (callback) {
    const ScopeUnlock unlock(mutex);
    callback();
  }
}
