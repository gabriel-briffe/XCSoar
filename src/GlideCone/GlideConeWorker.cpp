// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeWorker.hpp"
#include "LogFile.hpp"

bool
GlideConeWorker::Request(GlideConeGridRequest request,
                         const RasterTerrain *terrain,
                         const Waypoints *waypoints) noexcept
{
  try {
    const std::lock_guard lock{mutex};
    LogFmt("glidecones: worker.Request gen={} combined={} radius={:.0f}",
           request.generation, request.combined, request.radius_m);
    next = std::move(request);
    next_terrain = terrain;
    next_waypoints = waypoints;
    Trigger();
    return true;
  } catch (...) {
    /* thread failed to start; Draw keeps the last-good field */
    LogFmt("glidecones: worker.Request exception (thread start failed)");
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

  /* Take ownership under the lock (like the GPU worker) so we do not
     deep-copy seeds / settings while the draw thread waits. */
  GlideConeGridRequest request = std::move(next);
  next = {};
  const RasterTerrain *const terrain = next_terrain;
  const Waypoints *const waypoints = next_waypoints;
  next_terrain = nullptr;
  next_waypoints = nullptr;

  LogFmt("glidecones: worker.Tick start gen={} combined={}",
         request.generation, request.combined);

  std::unique_ptr<GlideConePreparedGrid> built;
  {
    const ScopeUnlock unlock{mutex};
    try {
      built = std::make_unique<GlideConePreparedGrid>();
      built->generation = request.generation;
      if (terrain == nullptr ||
          !BuildGlideConeGrid(request, *terrain, waypoints, *built)) {
        LogFmt("glidecones: worker.Tick build failed gen={}",
               request.generation);
        built->grid = {};
      }
    } catch (...) {
      LogFmt("glidecones: worker.Tick exception gen={}", request.generation);
      built.reset();
    }
  }

  /* A newer Request() replaces #next while we were unlocked. */
  if (next.generation != 0 &&
      request.generation != next.generation) {
    LogFmt("glidecones: worker.Tick drop gen={} (superseded by {})",
           request.generation, next.generation);
    return;
  }

  if (built == nullptr) {
    try {
      built = std::make_unique<GlideConePreparedGrid>();
      built->generation = request.generation;
    } catch (...) {
      return;
    }
  }

  LogFmt("glidecones: worker.Tick done gen={} valid={}",
         request.generation, built->grid.IsValid());
  ready = std::move(built);
}
