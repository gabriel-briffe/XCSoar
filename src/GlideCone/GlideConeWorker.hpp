// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeGridBuilder.hpp"
#include "thread/StandbyThread.hpp"

#include <memory>

class RasterTerrain;
class Waypoints;

/**
 * Background thread that builds a #GlideConePreparedGrid (window,
 * seeds, DEM max-pool).  No GL.  Terrain is sampled under
 * #RasterTerrain::Lease.
 */
class GlideConeWorker : private StandbyThread {
  GlideConeGridRequest next;
  const RasterTerrain *next_terrain = nullptr;
  const Waypoints *next_waypoints = nullptr;

  std::unique_ptr<GlideConePreparedGrid> ready;

public:
  GlideConeWorker() noexcept
    :StandbyThread("GlideCone") {}

  ~GlideConeWorker() noexcept {
    LockStop();
  }

  /**
   * Queue a grid build.  Safe from the draw thread.  A newer request
   * replaces a waiting one; an in-flight build is finished then
   * discarded if its generation is stale.
   *
   * @return false if the worker thread could not be started.
   */
  bool Request(GlideConeGridRequest request,
               const RasterTerrain *terrain,
               const Waypoints *waypoints) noexcept;

  /** Take the latest completed grid, or nullptr. */
  std::unique_ptr<GlideConePreparedGrid> TakeReady() noexcept;

private:
  void Tick() noexcept override;
};
