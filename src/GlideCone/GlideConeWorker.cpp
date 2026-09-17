// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeWorker.hpp"

bool
GlideConeWorker::Request(GlideConeGridRequest request,
                         const Waypoints *waypoints,
                         RasterTerrain *terrain) noexcept
{
  try {
    const std::lock_guard lock{mutex};
    next = std::move(request);
    next_waypoints = waypoints;
    next_terrain = terrain;
    Trigger();
    return true;
  } catch (...) {
    /* thread failed to start; Draw keeps the last-good field */
    return false;
  }
}

std::unique_ptr<GlideConePreparedGrid>
GlideConeWorker::TakeReady() noexcept
{
  const std::lock_guard lock{mutex};
  return std::move(ready);
}

void
GlideConeWorker::NotifyReady() noexcept
{
  if (ready_callback)
    ready_callback();
}

void
GlideConeWorker::Tick() noexcept
{
  SetIdlePriority();

  /* Take ownership under the lock (like the GPU worker) so we do not
     deep-copy seeds / settings while the draw thread waits. */
  GlideConeGridRequest request = std::move(next);
  next = {};
  const Waypoints *const waypoints = next_waypoints;
  next_waypoints = nullptr;
  RasterTerrain *const terrain = next_terrain;
  next_terrain = nullptr;

  std::unique_ptr<GlideConePreparedGrid> built;
  {
    const ScopeUnlock unlock{mutex};
    try {
      built = std::make_unique<GlideConePreparedGrid>();
      built->generation = request.generation;
      if (!BuildGlideConeGrid(request, dem, terrain, waypoints, *built)) {
        built->grid = {};
      }
    } catch (...) {
      built.reset();
    }
  }

  /* A newer Request() replaces #next while we were unlocked. */
  if (next.generation != 0 &&
      request.generation != next.generation) {
    NotifyReady();
    return;
  }

  if (built == nullptr) {
    try {
      built = std::make_unique<GlideConePreparedGrid>();
      built->generation = request.generation;
    } catch (...) {
      NotifyReady();
      return;
    }
  }

  ready = std::move(built);
  NotifyReady();
}
