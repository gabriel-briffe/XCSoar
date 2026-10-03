// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeRenderer.hpp"
#include "GlideConeOptions.hpp"
#include "Interface.hpp"
#include "NMEA/MoreData.hpp"
#include "GlideConeStatus.hpp"
#include "Computer/Settings.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
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
    if (required) {
      const auto path = field.Trace(aircraft);
      double path_distance = 0;
      unsigned destination_id = 0;
      if (path.size() >= 2) {
        for (std::size_t i = 1; i < path.size(); ++i) {
          const GeoPoint a = field.CellToGeo(path[i - 1].x, path[i - 1].y);
          const GeoPoint b = field.CellToGeo(path[i].x, path[i].y);
          if (!a.IsValid() || !b.IsValid()) {
            path_distance = 0;
            break;
          }
          path_distance += a.DistanceS(b);
        }
        const auto &last = path.back();
        for (const auto &s : field.seeds)
          if (s.x == last.x && s.y == last.y) {
            destination_id = s.waypoint_id;
            break;
          }
      }
      GlideConeStatus::Set({true, *required, path_distance, destination_id});
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

  if (aircraft_valid) {
    int gi = -1, gj = -1;
    const MoreData &basic = CommonInterface::Basic();
    if (field.GeoToCell(aircraft, gi, gj) && basic.NavAltitudeAvailable())
      GlideConeOptions::Update(field, basic.nav_altitude, gi, gj, gc);
  }
  GlideConeOptions::Draw(canvas, projection, gc);
  GlideConeOptions::DrawTimer(canvas, projection.GetScreenSize(),
                              *look.overlay.overlay_font, gc);

  overlay.DrawTraces(canvas, projection, field, aircraft, aircraft_valid,
                     pan_probe, look);
}
