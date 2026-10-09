// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeRenderer.hpp"
#include "GlideConeOptions.hpp"
#include "GlideConeDownward.hpp"
#include "Interface.hpp"
#include "LogFile.hpp"
#include "NMEA/MoreData.hpp"
#include "GlideConeStatus.hpp"
#include "Computer/Settings.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
#include "ui/canvas/Canvas.hpp"

#include "ui/dim/Point.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

void
GlideConeRenderer::SetTarget(GeoPoint seed, double elevation) noexcept
{
  const std::lock_guard lock{mutex};
  pending_seed = seed;
  pending_seed_alt = elevation;
  pending_valid = seed.IsValid();
  ++pending_generation;
}

void
GlideConeRenderer::ClearTarget() noexcept
{
  const std::lock_guard lock{mutex};
  pending_valid = false;
  ++pending_generation;
}

void
GlideConeRenderer::Draw(Canvas &canvas, const WindowProjection &projection,
                        GeoPoint aircraft, bool aircraft_valid,
                        GeoPoint target, bool target_valid,
                        const ComputerSettings &settings,
                        RasterTerrain *terrain,
                        const Waypoints *waypoints,
                        const WaypointRendererSettings &waypoint_settings,
                        const MapLook &look,
                        GeoPoint pan_probe) noexcept
{
  GeoPoint pending_seed_copy = GeoPoint::Invalid();
  bool pending_valid_copy = false;
  {
    const std::lock_guard lock{mutex};
    pending_seed_copy = pending_seed;
    pending_valid_copy = pending_valid;
  }

  if (!jobs.Update(field, overlay, aircraft, aircraft_valid,
                   target, target_valid,
                   pending_seed_copy, pending_valid_copy,
                   settings, terrain, waypoints, waypoint_settings)) {
    GlideConeStatus::SetInvalid();
    return;
  }

  DrawField(canvas, projection, aircraft, aircraft_valid, pan_probe,
            settings, look);
}

std::optional<double>
GlideConeRenderer::QueryStoredAltitude(GeoPoint location) const noexcept
{
  if (!location.IsValid() || !field.IsValid())
    return std::nullopt;
  return field.StoredAltitude(location);
}

std::optional<double>
GlideConeRenderer::QueryRidgeSoaringProofGlideConeAltitude(
  GeoPoint location) const noexcept
{
  if (!location.IsValid() || !field.IsValid())
    return std::nullopt;
  return field.RidgeSoaringProofGlideConeAltitude(location);
}

std::optional<double>
GlideConeRenderer::QueryOptionsAltitude(GeoPoint location) const noexcept
{
  return GlideConeOptions::QueryArrivalAltitude(location);
}

void
GlideConeRenderer::DrawField(Canvas &canvas,
                             const WindowProjection &projection,
                             GeoPoint aircraft, bool aircraft_valid,
                             GeoPoint pan_probe,
                             const ComputerSettings &settings,
                             const MapLook &look) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;

  if (!field.IsValid()) {
    GlideConeStatus::SetInvalid();
    return;
  }

  if (aircraft_valid) {
    const auto proof =
      field.RidgeSoaringProofGlideConeAltitude(aircraft);
    if (proof) {
      const auto path_distance = field.PathDistance(aircraft);
      const unsigned destination_id =
        field.PathDestinationWaypointId(aircraft);
      GlideConeStatus::Set({true, *proof,
                            path_distance.value_or(0), destination_id});
    } else {
      GlideConeStatus::SetInvalid();
    }
  } else {
    GlideConeStatus::SetInvalid();
  }

  if (gc.contours) {
    jobs.UpdateContours(field, overlay, gc);

    const bool hold_labels = jobs.IsAwaitingGrid() || jobs.IsAwaitingGpu() ||
      jobs.IsAwaitingContours() || overlay.IsHoldRebase();
    overlay.DrawContours(canvas, projection, field, gc, look, hold_labels,
                         jobs.IsAwaitingGrid(), jobs.IsAwaitingGpu(),
                         jobs.IsAwaitingContours());
  } else if (jobs.HasContourState() || !field.contour_lines.empty()) {
    jobs.ClearContours(field, overlay);
  }

  const MoreData &basic = CommonInterface::Basic();
  if (gc.options_mode == GlideConeSettings::OptionsMode::OFF)
    jobs.CancelDownward();
  if (auto ready = jobs.TakeDownward()) {
    if (ready->ok)
      GlideConeOptions::ApplyGpu(field, *ready, gc);
    else {
      LogFmt("GlideCone options: gpu result failed");
      GlideConeOptions::AbandonGpu();
    }
  }
  if (aircraft_valid) {
    int gi = -1, gj = -1;
    if (field.GeoToCell(aircraft, gi, gj) && basic.NavAltitudeAvailable()) {
      const bool gpu =
        gc.options_engine == GlideConeSettings::OptionsEngine::GPU;
      GlideConeDownwardJob gpu_job;
      GlideConeOptions::Update(field, basic.nav_altitude, aircraft,
                               gi, gj, gc, gpu ? &gpu_job : nullptr);
      if (gpu && gpu_job.width != 0) {
        LogFmt("GlideCone options: queue gpu {}x{} passes={}",
               gpu_job.width, gpu_job.height, gpu_job.passes.size());
        GlideConeOptions::NoteGpuQueued(gpu_job);
        if (!jobs.RequestDownward(
              std::make_unique<GlideConeDownwardJob>(std::move(gpu_job)))) {
          LogFmt("GlideCone options: gpu queue failed, using cpu");
          GlideConeOptions::AbandonGpu();
          GlideConeOptions::Update(field, basic.nav_altitude, aircraft,
                                   gi, gj, gc, nullptr);
        }
      }
    }
  }
  GlideConeOptions::Draw(canvas, projection, gc);

  std::optional<double> aircraft_altitude;
  if (aircraft_valid && basic.NavAltitudeAvailable())
    aircraft_altitude = basic.nav_altitude;
  std::optional<double> pan_altitude =
    pan_probe.IsValid() ? GlideConeOptions::QueryArrivalAltitude(pan_probe)
                        : std::nullopt;

  const bool draw_highest = gc.highest_arrival_route;
  bool highest_drawn = false;
  std::vector<PixelPoint> critical_discs;

  /* Worst-case path under the green path when both are on; discs
     are deferred so green cannot cover them. */
  const bool draw_worst =
    gc.worst_case_route == GlideConeSettings::WorstCaseRoute::ALWAYS ||
    !draw_highest;
  if (draw_worst)
    critical_discs =
      overlay.DrawTraces(canvas, projection, field, aircraft, aircraft_valid,
                         aircraft_altitude, pan_probe, pan_altitude, look);

  if (draw_highest)
    highest_drawn =
      GlideConeOptions::DrawBestAirportPath(canvas, projection, look);

  /* IF_BELOW: if the green path was requested but not available, fall
     back to the pink path (unless ALWAYS already drew it). */
  if (!draw_worst && !highest_drawn)
    critical_discs =
      overlay.DrawTraces(canvas, projection, field, aircraft, aircraft_valid,
                         aircraft_altitude, pan_probe, pan_altitude, look);

  /* Pan only: pink aircraft → option cell, green cell → airport. */
  if (pan_probe.IsValid() && gc.pan_mode_path)
    GlideConeOptions::DrawPanPaths(canvas, projection, pan_probe,
                                   field, look);

  overlay.DrawCriticalDiscs(canvas, critical_discs);
}
