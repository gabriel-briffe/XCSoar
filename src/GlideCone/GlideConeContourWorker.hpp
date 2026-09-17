// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeField.hpp"
#include "thread/StandbyThread.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

/**
 * Completed background contour build (stitched polylines) for a
 * glide-cone field generation.
 */
struct GlideConeContourReady {
  std::uint64_t generation = 0;
  std::vector<GlideConeField::ContourLine> contour_lines;
};

/**
 * Background thread that runs #GlideConeField::BuildContours.  No GL.
 * The draw thread queues a field snapshot and collects results.
 */
class GlideConeContourWorker : private StandbyThread {
  std::uint64_t next_generation = 0;
  double next_interval_m = 100;
  std::unique_ptr<GlideConeField> next_field;

  std::unique_ptr<GlideConeContourReady> ready;

  /** Called from the worker thread when a job attempt finishes. */
  std::function<void()> ready_callback;

public:
  GlideConeContourWorker() noexcept
    :StandbyThread("GlideConeContour") {}

  ~GlideConeContourWorker() noexcept {
    LockStop();
  }

  /**
   * Queue a contour build for @p field (moved).  A newer request
   * replaces a waiting one; an in-flight build is discarded if its
   * generation is stale.
   *
   * @return false if the worker thread could not be started.
   */
  bool Request(std::uint64_t generation, GlideConeField field,
               double interval_m = 100) noexcept;

  /** Take the latest completed contours, or nullptr. */
  std::unique_ptr<GlideConeContourReady> TakeReady() noexcept;

  void Cancel() noexcept;

  /** Thread-safe redraw wake-up when a Tick() finishes. */
  void SetReadyCallback(std::function<void()> callback) noexcept {
    const std::lock_guard lock{mutex};
    ready_callback = std::move(callback);
  }

  [[gnu::pure]]
  bool IsBusy() noexcept;

private:
  void NotifyReady() noexcept;

  void Tick() noexcept override;
};
