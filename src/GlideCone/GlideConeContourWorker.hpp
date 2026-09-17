// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeField.hpp"
#include "thread/StandbyThread.hpp"

#include <cstdint>
#include <memory>
#include <vector>

/**
 * Completed background contour build (raw segments or stitched
 * polylines) for a glide-cone field generation.
 */
struct GlideConeContourReady {
  std::uint64_t generation = 0;
  bool polylines = true;
  std::vector<GlideConeField::ContourLine> contour_lines;
};

/**
 * Background thread that runs #GlideConeField::BuildContours.  No GL.
 * The draw thread queues a field snapshot and collects results.
 */
class GlideConeContourWorker : private StandbyThread {
  std::uint64_t next_generation = 0;
  bool next_polylines = true;
  double next_interval_m = 100;
  std::unique_ptr<GlideConeField> next_field;

  std::unique_ptr<GlideConeContourReady> ready;

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
               bool polylines, double interval_m = 100) noexcept;

  /** Take the latest completed contours, or nullptr. */
  std::unique_ptr<GlideConeContourReady> TakeReady() noexcept;

  void Cancel() noexcept;

  [[gnu::pure]]
  bool IsBusy() noexcept;

private:
  void Tick() noexcept override;
};
