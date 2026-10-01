// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "thread/StandbyThread.hpp"
#include "Geo/GeoPoint.hpp"

#include <functional>

class RasterTerrain;
class WindowProjection;

/**
 * A thread that loads topography files asynchronously.
 */
class TerrainThread final : private StandbyThread {
  RasterTerrain &terrain;

  const std::function<void()> callback;

  GeoPoint last_center = GeoPoint::Invalid();
  double last_radius;
  double last_meters_per_pixel = -1;
  double last_scale_bar = -1;
  double last_dem_medium_scale = -1;
  double last_dem_coarse_scale = -1;

  GeoPoint next_center;
  double next_radius;
  double next_meters_per_pixel = 0;
  double next_scale_bar = 0;
  double next_dem_medium_scale = 0;
  double next_dem_coarse_scale = 0;

public:
  TerrainThread(RasterTerrain &_terrain, std::function<void()> &&_callback);

  using StandbyThread::LockStop;

  void Trigger(const WindowProjection &projection,
               double dem_medium_scale,
               double dem_coarse_scale);

private:
  /* virtual methods from class StandbyThread*/
  void Tick() noexcept override;
};
