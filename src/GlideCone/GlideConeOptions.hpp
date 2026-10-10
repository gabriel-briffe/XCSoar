// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct GlideConeField;
struct GlideConeSettings;
struct GlideConeDownwardJob;
struct GlideConeDownwardReady;
struct GlideConeOptionsCpuJob;
struct GlideConeOptionsCpuReady;
struct MapLook;
class Canvas;
class WindowProjection;

/**
 * Optional-area mask: cells reachable downhill from the aircraft while
 * staying above ground clearance and the airport cone.  Arrival uses a
 * straight line to the last visible origin, matching the GPU shader.
 */
namespace GlideConeOptions {

void RequestOnce() noexcept;
void Clear() noexcept;

[[gnu::pure]]
std::optional<unsigned> LastComputeMs() noexcept;

/**
 * Descending arrival altitude [m MSL] at @p location when that cell is
 * in the optional area; nullopt if options are inactive or the cell is
 * outside the green patch.
 */
[[gnu::pure]]
std::optional<double> QueryArrivalAltitude(GeoPoint location) noexcept;

/**
 * Schedule or prepare optional-area work.
 *
 * CPU: when @p cpu_job_out is non-null and a run is due, fills it with a
 * field snapshot for #GlideConeOptionsWorker (does not compute on the
 * caller thread).  GPU: fills @p gpu_job when non-null.
 */
void Update(const GlideConeField &field, double start_alt,
            GeoPoint start_location, int gi, int gj,
            const GlideConeSettings &settings,
            GlideConeDownwardJob *gpu_job = nullptr,
            std::unique_ptr<GlideConeOptionsCpuJob> *cpu_job_out =
              nullptr) noexcept;

/**
 * Run CPU optional-area downward on a worker thread.  Fills @p out
 * (ok=false when there is no seed / empty area).
 */
void ComputeCpu(const GlideConeOptionsCpuJob &job,
                GlideConeOptionsCpuReady &out) noexcept;

/**
 * Install a finished CPU downward result.  Ignores a stale generation.
 */
void ApplyCpu(const GlideConeField &field,
              GlideConeOptionsCpuReady &&ready,
              const GlideConeSettings &settings) noexcept;

/**
 * Install a finished GPU downward result.  Ignores a stale generation.
 */
void ApplyGpu(const GlideConeField &field,
              const GlideConeDownwardReady &ready,
              const GlideConeSettings &settings) noexcept;

/** Mark the job just passed to the compute thread as the one in flight. */
void NoteGpuQueued(GlideConeDownwardJob &job) noexcept;

/** Mark the CPU job as in flight (generation already set on the job). */
void NoteCpuQueued() noexcept;

/**
 * Drop the in-flight GPU generation.  Keeps the routine cooldown so the
 * next map frame does not immediately start another GPU job.
 */
void AbandonGpu() noexcept;

/**
 * Drop the in-flight CPU generation.  Keeps the routine cooldown.
 */
void AbandonCpu() noexcept;

void Draw(Canvas &canvas, const WindowProjection &projection,
          const GlideConeSettings &settings) noexcept;

/**
 * Aircraft → airport with the highest options arrival (green).
 *
 * @return true if drawn (omit the pink glide-cone path); false if the
 *         caller should draw the pink cone path instead.
 */
bool DrawBestAirportPath(Canvas &canvas, const WindowProjection &projection,
                         const MapLook &look) noexcept;

/**
 * Pan mode only: pink aircraft → option cell under the probe, then
 * (after settle debounce) a fresh downward from that cell and a green
 * path to the highest-arrival airport.
 */
bool DrawPanPaths(Canvas &canvas, const WindowProjection &projection,
                  GeoPoint pan_probe, const GlideConeField &field,
                  const MapLook &look) noexcept;

} // namespace GlideConeOptions
