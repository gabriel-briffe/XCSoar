// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeGridBuilder.hpp"
#include "GlideConeDemSampler.hpp"
#include "thread/StandbyThread.hpp"

#include <functional>
#include <memory>

class RasterTerrain;
class Waypoints;

/**
 * Background thread that builds a #GlideConePreparedGrid (window,
 * seeds, DEM max-pool).  No GL.  DEM comes from #GlideConeDemSampler
 * (private tiles; layout from the display #RasterTerrain).
 */
class GlideConeWorker : private StandbyThread {
  GlideConeGridRequest next;
  const Waypoints *next_waypoints = nullptr;
  RasterTerrain *next_terrain = nullptr;

  GlideConeDemSampler dem;

  std::unique_ptr<GlideConePreparedGrid> ready;

  /** Called from the worker thread when a job attempt finishes. */
  std::function<void()> ready_callback;

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
               const Waypoints *waypoints,
               RasterTerrain *terrain) noexcept;

  /** Take the latest completed grid, or nullptr. */
  std::unique_ptr<GlideConePreparedGrid> TakeReady() noexcept;

  /** Thread-safe redraw wake-up when a Tick() finishes. */
  void SetReadyCallback(std::function<void()> callback) noexcept {
    const std::lock_guard lock{mutex};
    ready_callback = std::move(callback);
  }

private:
  void NotifyReady() noexcept;

  void Tick() noexcept override;
};
