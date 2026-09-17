// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeGridBuilder.hpp"
#include "GlideConeData.hpp"

#include <cstdint>
#include <functional>
#include <memory>

/**
 * Completed GPU propagate job (CPU-side result + the prepared grid
 * metadata needed to install a #GlideConeField).
 */
struct GlideConeGpuReady {
  std::unique_ptr<GlideConePreparedGrid> prepared;
  GlideConeResult result;
  bool ok = false;
  /** True when Finish succeeded but the iteration cap was exhausted. */
  bool hit_iteration_cap = false;
};

/**
 * Background GLES 3.1 compute thread for glide-cone propagation.
 *
 * Owns a shared EGL context (share group with the UI context).  The
 * draw thread only queues grids and collects results — no Step()
 * during paint.
 */
class GlideConeGpuWorker {
public:
  GlideConeGpuWorker() noexcept;
  ~GlideConeGpuWorker() noexcept;

  /**
   * Queue a grid for GPU propagate.  Must be called with the UI EGL
   * context current (draw thread) so a shared compute context can be
   * created on first use.
   *
   * @return false if the shared context could not be created / thread
   *         could not start.
   */
  bool Request(std::unique_ptr<GlideConePreparedGrid> prepared) noexcept;

  /** Latest completed job, or nullptr. */
  std::unique_ptr<GlideConeGpuReady> TakeReady() noexcept;

  /** Ask the in-flight job to abort at the next batch boundary. */
  void Cancel() noexcept;

  /** Thread-safe redraw wake-up when a Tick() finishes. */
  void SetReadyCallback(std::function<void()> callback) noexcept;

  [[gnu::pure]]
  bool IsBusy() const noexcept;

private:
  struct Impl;
  Impl *impl;
};
