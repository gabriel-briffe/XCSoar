// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeRenderer.hpp"
#include "GlideConeStatus.hpp"
#include "Computer/Settings.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
#include "LogFile.hpp"
#include "ui/canvas/Canvas.hpp"

#include <algorithm>

void
GlideConeRenderer::SetTarget(GeoPoint seed, double elevation) noexcept
{
  const std::lock_guard lock{mutex};
  pending_seed = seed;
  pending_seed_alt = elevation;
  pending_valid = seed.IsValid();
  ++pending_generation;
  if (pending_valid)
    LogFmt("glidecones: SetTarget gen={} lat={:.5f} lon={:.5f} elev={:.0f}",
           pending_generation, seed.latitude.Degrees(),
           seed.longitude.Degrees(), elevation);
  else
    LogFmt("glidecones: SetTarget gen={} invalid", pending_generation);
}

void
GlideConeRenderer::ClearTarget() noexcept
{
  const std::lock_guard lock{mutex};
  pending_valid = false;
  ++pending_generation;
  LogFmt("glidecones: ClearTarget gen={}", pending_generation);
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
GlideConeRenderer::QueryRequiredAltitude(GeoPoint location) const noexcept
{
  if (!location.IsValid() || !field.IsValid())
    return std::nullopt;
  return field.RequiredAltitude(location);
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
    const auto required = field.RequiredAltitude(aircraft);
    if (required)
      GlideConeStatus::Set({true, *required});
    else
      GlideConeStatus::SetInvalid();
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

  overlay.DrawTraces(canvas, projection, field, aircraft, aircraft_valid,
                     pan_probe, look);
}
