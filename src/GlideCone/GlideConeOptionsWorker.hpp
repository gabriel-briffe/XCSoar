// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeField.hpp"
#include "Settings.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"
#include "thread/StandbyThread.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

/**
 * Background CPU optional-area job (field snapshot + seed inputs).
 */
struct GlideConeOptionsCpuJob {
  std::uint64_t generation = 0;
  GlideConeField field;
  double start_alt = 0;
  GeoPoint start_location = GeoPoint::Invalid();
  int gi = -1;
  int gj = -1;
  GlideConeSettings::OptionsDisplay display =
    GlideConeSettings::OptionsDisplay::MARGIN;
};

/**
 * Completed CPU optional-area run for the draw thread to install.
 */
struct GlideConeOptionsCpuReady {
  std::uint64_t generation = 0;
  bool ok = false;
  unsigned compute_ms = 0;

  unsigned width = 0, height = 0;
  GeoBounds bounds = GeoBounds::Invalid();
  double cell_x = 0, cell_y = 0;
  int aircraft_gi = -1, aircraft_gj = -1;
  GeoPoint start_location = GeoPoint::Invalid();
  int start_index = -1;

  std::vector<std::uint8_t> mask;
  std::vector<float> arrival;
  std::vector<int> origin;
  std::vector<float> floors;
  std::vector<std::uint8_t> mask10;
  std::vector<std::uint8_t> mask20;
  GlideConeSettings::OptionsDisplay display =
    GlideConeSettings::OptionsDisplay::MARGIN;
};

/**
 * Background thread for CPU optional-area downward propagate.
 * No GL.  Draw thread queues a field snapshot and collects results.
 */
class GlideConeOptionsWorker : private StandbyThread {
  std::unique_ptr<GlideConeOptionsCpuJob> next;
  std::unique_ptr<GlideConeOptionsCpuReady> ready;

  std::function<void()> ready_callback;

public:
  GlideConeOptionsWorker() noexcept
    :StandbyThread("GlideConeOptions") {}

  ~GlideConeOptionsWorker() noexcept {
    LockStop();
  }

  /**
   * Queue a CPU options job.  A newer request replaces a waiting one;
   * an in-flight job is finished then discarded if stale.
   */
  bool Request(std::unique_ptr<GlideConeOptionsCpuJob> job) noexcept;

  std::unique_ptr<GlideConeOptionsCpuReady> TakeReady() noexcept;

  void Cancel() noexcept;

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
