// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeWorker.hpp"
#include "GlideConeCpu.hpp"
#include "LogFile.hpp"

#include <chrono>

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
  using Clock = std::chrono::steady_clock;
  const auto t0 = Clock::now();
  unsigned grid_ms = 0;
  unsigned cpu_ms = 0;
  {
    const ScopeUnlock unlock{mutex};
    try {
      built = std::make_unique<GlideConePreparedGrid>();
      built->generation = request.generation;
      const auto t_grid = Clock::now();
      if (!BuildGlideConeGrid(request, dem, terrain, waypoints, *built)) {
        built->grid = {};
        LogFmt("GlideCone cone: worker grid failed gen={} cpu_prop={}",
               request.generation, request.cpu_propagate);
      } else {
        grid_ms = unsigned(std::chrono::duration_cast<
          std::chrono::milliseconds>(Clock::now() - t_grid).count());
        LogFmt("GlideCone cone: worker grid gen={} {}x{} seeds={} "
               "{}ms cpu_prop={}",
               request.generation, built->grid.width, built->grid.height,
               built->grid.seeds.size(), grid_ms, request.cpu_propagate);
        if (request.cpu_propagate && built->grid.IsValid()) {
          /* Stale jobs are dropped after unlock (same as grid-only). */
          const auto t_cpu = Clock::now();
          built->cpu_ok = PropagateUpwardCpu(built->grid, {},
                                             built->cpu_result,
                                             &built->hit_iteration_cap);
          cpu_ms = unsigned(std::chrono::duration_cast<
            std::chrono::milliseconds>(Clock::now() - t_cpu).count());
          if (!built->cpu_ok) {
            built->cpu_result.Clear();
            LogFmt("GlideCone cone: worker cpu failed gen={} {}ms",
                   request.generation, cpu_ms);
          }
        }
      }
    } catch (...) {
      built.reset();
      LogFmt("GlideCone cone: worker exception gen={}",
             request.generation);
    }
  }

  /* A newer Request() replaces #next while we were unlocked. */
  if (next.generation != 0 &&
      request.generation != next.generation) {
    LogFmt("GlideCone cone: worker drop stale gen={} next={}",
           request.generation, next.generation);
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

  const unsigned total_ms = unsigned(std::chrono::duration_cast<
    std::chrono::milliseconds>(Clock::now() - t0).count());
  if (built != nullptr) {
    built->compute_ms = total_ms;
    LogFmt("GlideCone cone: worker ready gen={} grid_ok={} cpu_ok={} "
           "hit_cap={} grid={}ms cpu={}ms total={}ms",
           request.generation, built->grid.IsValid(), built->cpu_ok,
           built->hit_iteration_cap, grid_ms, cpu_ms, total_ms);
  }

  ready = std::move(built);
  NotifyReady();
}
