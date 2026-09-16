// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeRenderer.hpp"
#include "GlideConeCompute.hpp"
#include "GlideConeStatus.hpp"
#include "Computer/Settings.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
#include "Renderer/TextInBox.hpp"
#include "Renderer/LabelBlock.hpp"
#include "Formatter/UserUnits.hpp"
#include "Screen/Layout.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoClip.hpp"
#include "Math/Angle.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Color.hpp"
#include "ui/dim/BulkPoint.hpp"
#include "LogFile.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>
#include <vector>

/** Combined-mode recompute threshold as a fraction of the window half-width
    (see gpu-MC AUTO_MAX_OFFSET_FROM_CENTER). */
static constexpr double GLIDE_CONE_MAX_OFFSET_FROM_CENTER = 0.25;

/** Single-mode recompute threshold when the target moves [m]. */
static constexpr double GLIDE_CONE_SEED_EPSILON_M = 50;

/** Debounce for parameter / terrain-tile changes. */
static constexpr std::chrono::milliseconds GLIDE_CONE_DEBOUNCE{400};

[[gnu::pure]]
static const char *
ModeName(GlideConeSettings::Mode mode) noexcept
{
  switch (mode) {
  case GlideConeSettings::Mode::OFF:
    return "off";
  case GlideConeSettings::Mode::SINGLE:
    return "single";
  case GlideConeSettings::Mode::COMBINED:
    return "combined";
  }
  return "?";
}

/**
 * Draw a geo polyline clipped to the visible map.  Unclipped
 * GeoToScreen points can sit far off-screen when zoomed in; connecting
 * them produces lines that slash across the viewport.
 */
static void
DrawClippedGeoPolyline(Canvas &canvas, const WindowProjection &projection,
                       const GeoClip &clip,
                       std::span<const GeoPoint> points) noexcept
{
  if (points.size() < 2)
    return;

  for (std::size_t i = 1; i < points.size(); ++i) {
    GeoPoint a = points[i - 1];
    GeoPoint b = points[i];
    if (!a.IsValid() || !b.IsValid())
      continue;
    if (!clip.ClipLine(a, b))
      continue;

    canvas.DrawLine(projection.GeoToScreen(a),
                    projection.GeoToScreen(b));
  }
}

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

[[gnu::pure]]
static std::size_t
SettingsSignature(const GlideConeSettings &s,
                  double clearance, double arrival) noexcept
{
  std::size_t h = std::hash<int>{}(int(s.mode));
  h = h * 31 + std::hash<double>{}(s.glide_ratio);
  h = h * 31 + std::hash<double>{}(s.max_altitude);
  h = h * 31 + std::hash<double>{}(s.cell_size);
  h = h * 31 + std::hash<unsigned>{}(s.iteration_cap);
  h = h * 31 + std::hash<double>{}(clearance);
  h = h * 31 + std::hash<double>{}(arrival);
  return h;
}

[[gnu::pure]]
static std::size_t
WaypointDisplaySignature(const WaypointRendererSettings &s) noexcept
{
  std::size_t h = std::hash<bool>{}(s.display_non_icao_airports);
  for (unsigned i = 0; i < unsigned(Waypoint::Type::COUNT); ++i)
    h = h * 31 + std::hash<bool>{}(s.display_types[i]);
  return h;
}

void
GlideConeRenderer::AdjustTerrainCoverage(const ComputerSettings &settings,
                                         GeoPoint aircraft,
                                         bool aircraft_valid,
                                         GeoPoint target, bool target_valid,
                                         GeoPoint &location,
                                         double &radius) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;
  if (!gc.IsEnabled())
    return;

  radius = std::max(radius, gc.WindowRadiusM());

  if (gc.mode == GlideConeSettings::Mode::COMBINED && aircraft_valid)
    location = aircraft;
  else if (gc.mode == GlideConeSettings::Mode::SINGLE && target_valid)
    location = target;
}

void
GlideConeRenderer::AbortJobs() noexcept
{
  const bool busy = awaiting_grid || awaiting_gpu || gpu_worker.IsBusy();
  if (busy)
    LogFmt("glidecones: AbortJobs gen={} awaiting_grid={} awaiting_gpu={} "
           "gpu_busy={}",
           job_generation, awaiting_grid, awaiting_gpu, gpu_worker.IsBusy());
  gpu_worker.Cancel();
  awaiting_grid = false;
  awaiting_gpu = false;
  if (busy)
    ++job_generation;
  (void)worker.TakeReady();
  (void)gpu_worker.TakeReady();
}

void
GlideConeRenderer::InstallField(GlideConePreparedGrid &&prepared,
                                GlideConeResult &&result) noexcept
{
  LogFmt("glidecones: InstallField gen={} {}x{} seeds={} cell={:.0f}x{:.0f}m",
         prepared.generation, result.width, result.height,
         prepared.grid.seeds.size(),
         prepared.grid.cell_size_x_m, prepared.grid.cell_size_y_m);
  field.result = std::move(result);
  field.bounds = prepared.bounds;
  field.cell_size_m = std::sqrt(prepared.grid.cell_size_x_m *
                                prepared.grid.cell_size_y_m);
  field.cell_size_x_m = prepared.grid.cell_size_x_m;
  field.cell_size_y_m = prepared.grid.cell_size_y_m;
  field.glide_ratio = prepared.grid.glide_ratio;
  field.max_alt = prepared.grid.max_alt;
  field.home_x = prepared.grid.seeds.front().x;
  field.home_y = prepared.grid.seeds.front().y;
  field.seeds = std::move(prepared.grid.seeds);
  field.elevation = std::move(prepared.grid.elevation);
  computed_contours = false;
}

void
GlideConeRenderer::Draw(Canvas &canvas, const WindowProjection &projection,
                        GeoPoint aircraft, bool aircraft_valid,
                        GeoPoint target, bool target_valid,
                        const ComputerSettings &settings,
                        const RasterTerrain *terrain,
                        const Waypoints *waypoints,
                        const WaypointRendererSettings &waypoint_settings,
                        const MapLook &look) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;
  const auto mode = gc.mode;
  const double clearance =
    settings.task.route_planner.safety_height_terrain;
  const double arrival = settings.task.safety_height_arrival;

  std::size_t signature = SettingsSignature(gc, clearance, arrival);
  if (mode == GlideConeSettings::Mode::COMBINED)
    signature = signature * 31 + WaypointDisplaySignature(waypoint_settings);

  if (mode == GlideConeSettings::Mode::OFF || terrain == nullptr ||
      !GlideConeGpuSession::Available()) {
    if (field.IsValid() || awaiting_grid || awaiting_gpu ||
        gpu_worker.IsBusy())
      LogFmt("glidecones: idle mode={} terrain={} gpu_avail={} "
             "(clearing pipeline)",
             ModeName(mode), terrain != nullptr,
             GlideConeGpuSession::Available());
    AbortJobs();
    field.Clear();
    computed_center = GeoPoint::Invalid();
    GlideConeStatus::SetInvalid();
    return;
  }

  GeoPoint center = GeoPoint::Invalid();
  const double radius_m = gc.WindowRadiusM();
  double recompute_threshold_m = GLIDE_CONE_SEED_EPSILON_M;
  std::vector<GeoPoint> single_seeds;
  bool have_center = false;
  const char *seed_source = "-";

  if (mode == GlideConeSettings::Mode::SINGLE) {
    GeoPoint seed;
    bool valid;
    if (target_valid) {
      seed = target;
      valid = true;
      seed_source = "map_target";
    } else {
      const std::lock_guard lock{mutex};
      seed = pending_seed;
      valid = pending_valid;
      seed_source = "pending";
    }

    if (!valid) {
      if (field.IsValid() || awaiting_grid || awaiting_gpu)
        LogFmt("glidecones: single: no seed (target_valid={} pending={})",
               target_valid, pending_valid);
      AbortJobs();
      field.Clear();
      computed_center = GeoPoint::Invalid();
      GlideConeStatus::SetInvalid();
      return;
    }

    center = seed;
    single_seeds.push_back(seed);
    have_center = true;
    recompute_threshold_m = GLIDE_CONE_SEED_EPSILON_M;
  } else if (aircraft_valid && waypoints != nullptr) {
    center = aircraft;
    have_center = true;
    seed_source = "aircraft";
    recompute_threshold_m = GLIDE_CONE_MAX_OFFSET_FROM_CENTER * radius_m;
  } else if (mode == GlideConeSettings::Mode::COMBINED) {
    if (field.IsValid() || awaiting_grid || awaiting_gpu)
      LogFmt("glidecones: combined: waiting aircraft={} waypoints={}",
             aircraft_valid, waypoints != nullptr);
  }

  const auto now = std::chrono::steady_clock::now();
  const bool sig_changed = signature != computed_signature;
  if (sig_changed && signature != debounce_signature) {
    debounce_signature = signature;
    debounce_since = now;
  }
  const bool sig_ready = sig_changed &&
    now - debounce_since >= GLIDE_CONE_DEBOUNCE;

  const Serial terrain_serial = terrain->GetSerial();
  if (terrain_serial != computed_terrain_serial &&
      terrain_serial != debounce_terrain_serial) {
    debounce_terrain_serial = terrain_serial;
    terrain_debounce_since = now;
  }
  const bool terrain_ready = field.IsValid() &&
    terrain_serial != computed_terrain_serial &&
    now - terrain_debounce_since >= GLIDE_CONE_DEBOUNCE;

  bool waypoints_ready = false;
  Serial waypoint_serial{};
  if (mode == GlideConeSettings::Mode::COMBINED && waypoints != nullptr) {
    waypoint_serial = waypoints->GetSerial();
    if (waypoint_serial != computed_waypoint_serial &&
        waypoint_serial != debounce_waypoint_serial) {
      debounce_waypoint_serial = waypoint_serial;
      waypoint_debounce_since = now;
    }
    waypoints_ready = field.IsValid() &&
      waypoint_serial != computed_waypoint_serial &&
      now - waypoint_debounce_since >= GLIDE_CONE_DEBOUNCE;
  }

  const bool center_moved = have_center &&
    (!computed_center.IsValid() ||
     computed_center.DistanceS(center) > recompute_threshold_m);

  const bool need_job = have_center &&
    (center_moved || sig_ready || terrain_ready || waypoints_ready);

  if (need_job) {
    ++job_generation;
    gpu_worker.Cancel();
    awaiting_gpu = false;
    (void)gpu_worker.TakeReady();

    LogFmt("glidecones: {} request gen={} via={} lat={:.5f} lon={:.5f} "
           "radius={:.0f}m L/D={:.0f} max_alt={:.0f} cell={:.0f} "
           "why: center={} sig={} terrain={} wpts={} seeds={}",
           ModeName(mode), job_generation, seed_source,
           center.latitude.Degrees(), center.longitude.Degrees(),
           radius_m, gc.glide_ratio, gc.max_altitude, gc.cell_size,
           center_moved, sig_ready, terrain_ready, waypoints_ready,
           single_seeds.size());

    GlideConeGridRequest request;
    request.generation = job_generation;
    request.center = center;
    request.radius_m = radius_m;
    request.glide_ratio = gc.glide_ratio;
    request.max_altitude = gc.max_altitude;
    request.cell_size = gc.cell_size;
    request.iteration_cap = gc.iteration_cap;
    request.clearance = clearance;
    request.arrival = arrival;
    request.combined = mode == GlideConeSettings::Mode::COMBINED;
    request.seeds = std::move(single_seeds);
    request.waypoint_settings = waypoint_settings;
    if (worker.Request(std::move(request), terrain, waypoints)) {
      awaiting_grid = true;
      computed_center = center;
      computed_signature = signature;
      computed_terrain_serial = terrain_serial;
      if (mode == GlideConeSettings::Mode::COMBINED)
        computed_waypoint_serial = waypoint_serial;
    } else {
      awaiting_grid = false;
      LogFmt("glidecones: worker.Request failed gen={}", job_generation);
    }
  }

  if (auto prepared = worker.TakeReady()) {
    if (prepared->generation == job_generation) {
      awaiting_grid = false;
      if (prepared->grid.IsValid()) {
        LogFmt("glidecones: grid ready gen={} {}x{} seeds={} "
               "cell={:.0f}x{:.0f}m → GPU worker",
               prepared->generation, prepared->grid.width,
               prepared->grid.height, prepared->grid.seeds.size(),
               prepared->grid.cell_size_x_m, prepared->grid.cell_size_y_m);
        if (gpu_worker.Request(std::move(prepared)))
          awaiting_gpu = true;
        else
          LogFmt("glidecones: GPU worker.Request failed gen={}",
                 job_generation);
      } else {
        LogFmt("glidecones: grid ready but invalid gen={} "
               "(build failed or empty seeds)",
               prepared->generation);
      }
    } else {
      LogFmt("glidecones: drop stale grid gen={} (current={})",
             prepared->generation, job_generation);
    }
  }

  if (auto gpu_ready = gpu_worker.TakeReady()) {
    awaiting_gpu = false;
    if (gpu_ready->prepared != nullptr &&
        gpu_ready->prepared->generation == job_generation &&
        gpu_ready->ok && gpu_ready->result.IsValid()) {
      LogFmt("glidecones: GPU Finish ok gen={}",
             gpu_ready->prepared->generation);
      InstallField(std::move(*gpu_ready->prepared),
                   std::move(gpu_ready->result));
    } else {
      LogFmt("glidecones: GPU Finish failed/stale gen={}", job_generation);
    }
  } else if (!gpu_worker.IsBusy()) {
    awaiting_gpu = false;
  }

  DrawField(canvas, projection, aircraft, aircraft_valid, settings, look);
}

void
GlideConeRenderer::DrawField(Canvas &canvas,
                             const WindowProjection &projection,
                             GeoPoint aircraft, bool aircraft_valid,
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
    if (!computed_contours) {
      field.BuildContours();
      computed_contours = true;
    }

    const bool show = projection.GetMapScale() <= gc.contours_min_scale;

    if (show && !field.contour_lines.empty()) {
      const PixelRect screen = projection.GetScreenRect();
      const GeoClip clip(projection.GetScreenBounds().Scale(1.1));

      canvas.Select(look.glide_cone_contour_pen);
      for (const auto &line : field.contour_lines)
        DrawClippedGeoPolyline(canvas, projection, clip, line.points);

      /* labels along each line, offset beside the stroke (MapLibre-style
         line text-offset), rotated parallel and flipped upright.
         All labels: no text overlap (LabelBlock).
         Same altitude: also min screen distance (label_spacing %). */
      if (look.overlay.overlay_font != nullptr) {
        canvas.Select(*look.overlay.overlay_font);
        canvas.SetBackgroundTransparent();
        LabelBlock label_block;
        label_block.reset();

        const unsigned short_side =
          std::min(screen.GetWidth(), screen.GetHeight());
        const unsigned pct = std::clamp(gc.label_spacing, 20u, 100u);
        const double spacing =
          std::max(20.0, double(short_side) * double(pct) / 100.0);
        const double spacing2 = spacing * spacing;
        /* sample candidates along the line (~1 em) */
        const double sample_step =
          std::max(8.0, double(look.overlay.overlay_font->GetHeight()));

        /* placed label centres, for same-altitude distance checks */
        std::vector<std::pair<int, PixelPoint>> placed;

        for (const auto &line : field.contour_lines) {
          if (line.points.size() < 2)
            continue;

          char buffer[32];
          FormatUserAltitude(double(line.level), buffer);
          const PixelSize ts = canvas.CalcTextSize(buffer);
          const double hw = ts.width / 2.0, hh = ts.height / 2.0;
          /* Offset off the line (~0.6 em) so text is not on the stroke. */
          const double offset_px = std::max(2.0, hh * 1.2);

          double since_sample = sample_step;
          PixelPoint prev = projection.GeoToScreen(line.points[0]);
          for (std::size_t k = 1; k < line.points.size(); ++k) {
            const PixelPoint cur = projection.GeoToScreen(line.points[k]);
            const double dx = cur.x - prev.x, dy = cur.y - prev.y;
            const double seg = std::hypot(dx, dy);
            since_sample += seg;
            prev = cur;
            if (since_sample < sample_step || seg < 1e-3)
              continue;
            since_sample = 0;

            if (cur.x < screen.left || cur.x > screen.right ||
                cur.y < screen.top || cur.y > screen.bottom)
              continue;

            double a = std::atan2(dy, dx);
            if (std::cos(a) < 0)
              a += M_PI;
            const double ca = std::cos(a), sa = std::sin(a);

            /* Perpendicular offset "above" upright text (screen y down). */
            const int lx = cur.x + int(std::lround(sa * offset_px));
            const int ly = cur.y + int(std::lround(-ca * offset_px));

            /* same altitude: enforce label distance in screen space */
            bool too_close = false;
            for (const auto &p : placed) {
              if (p.first != line.level)
                continue;
              const double ddx = double(p.second.x - lx);
              const double ddy = double(p.second.y - ly);
              if (ddx * ddx + ddy * ddy < spacing2) {
                too_close = true;
                break;
              }
            }
            if (too_close)
              continue;

            /* any label: no overlapping text */
            const int aabb_w = int(std::abs(hw * ca) + std::abs(hh * sa)) + 1;
            const int aabb_h = int(std::abs(hw * sa) + std::abs(hh * ca)) + 1;
            const PixelRect rc{lx - aabb_w, ly - aabb_h,
                               lx + aabb_w, ly + aabb_h};
            if (!label_block.check(rc))
              continue;

            placed.emplace_back(line.level, PixelPoint{lx, ly});
            const PixelPoint label_pos{lx, ly};

#ifdef ENABLE_OPENGL
            const Angle angle = Angle::Radians(a);
            canvas.SetTextColor(COLOR_WHITE);
            for (const auto off : {PixelPoint{-1, -1}, PixelPoint{1, -1},
                                   PixelPoint{-1, 1}, PixelPoint{1, 1}})
              canvas.DrawText({label_pos.x + off.x, label_pos.y + off.y},
                              buffer, angle);
            canvas.SetTextColor(COLOR_BLACK);
            canvas.DrawText(label_pos, buffer, angle);
#else
            RenderShadowedText(canvas, buffer,
                               {label_pos.x - int(ts.width) / 2,
                                label_pos.y - int(ts.height) / 2}, false);
#endif
          }
        }
      }
    }
  } else if (computed_contours) {
    field.contour_lines.clear();
    computed_contours = false;
  }

  if (!aircraft_valid)
    return;

  const std::vector<GlideConeField::TraceCell> cells = field.Trace(aircraft);
  if (cells.size() < 2)
    return;

  std::vector<BulkPixelPoint> run;
  bool run_ground = false;
  const auto flush = [&]() {
    if (run.size() < 2)
      return;
    if (run_ground) {
      canvas.Select(look.glide_cone_ground_pen);
      for (std::size_t i = 1; i < run.size(); ++i)
        canvas.DrawLine(run[i - 1], run[i]);
    } else {
      canvas.Select(look.glide_cone_pen);
      canvas.DrawPolyline(run.data(), unsigned(run.size()));
    }
  };

  for (std::size_t i = 1; i < cells.size(); ++i) {
    const auto &from = cells[i - 1];
    const auto &to = cells[i];
    const bool ground = field.IsDownhillGroundSegment(from.x, from.y,
                                                      to.x, to.y);
    const BulkPixelPoint from_pt = projection.GeoToScreen(
      field.CellToGeo(from.x, from.y));
    const BulkPixelPoint to_pt = projection.GeoToScreen(
      field.CellToGeo(to.x, to.y));

    if (run.empty()) {
      run_ground = ground;
      run.push_back(from_pt);
      run.push_back(to_pt);
      continue;
    }

    if (ground == run_ground) {
      run.push_back(to_pt);
      continue;
    }

    flush();
    run.clear();
    run_ground = ground;
    run.push_back(from_pt);
    run.push_back(to_pt);
  }
  flush();
}
