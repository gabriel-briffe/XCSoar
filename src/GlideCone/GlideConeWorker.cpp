// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeWorker.hpp"

bool
GlideConeWorker::Request(GlideConeGridRequest request,
                         const RasterTerrain *terrain,
                         const Waypoints *waypoints) noexcept
{
  try {
    const std::lock_guard lock{mutex};
    next = std::move(request);
    next_terrain = terrain;
    next_waypoints = waypoints;
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
GlideConeWorker::Tick() noexcept
{
  SetIdlePriority();

  const GlideConeGridRequest request = next;
  const RasterTerrain *const terrain = next_terrain;
  const Waypoints *const waypoints = next_waypoints;

  std::unique_ptr<GlideConePreparedGrid> built;
  {
    const ScopeUnlock unlock{mutex};
    try {
      built = std::make_unique<GlideConePreparedGrid>();
      built->generation = request.generation;
      if (terrain == nullptr ||
          !BuildGlideConeGrid(request, *terrain, waypoints, *built))
        built->grid = {};
    } catch (...) {
      built.reset();
    }
  }

  if (request.generation != next.generation)
    return;

  if (built == nullptr) {
    try {
      built = std::make_unique<GlideConePreparedGrid>();
      built->generation = request.generation;
    } catch (...) {
      return;
    }
  }
  ready = std::move(built);
}
