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
#include "Message.hpp"
#include "Language/Language.hpp"
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
  /* Terrain/settings refreshes keep the same grid origin; dropping the
     geo cache would re-place labels from the current screen and make
     them jump while flying.  Only rebase after a real center move. */
  if (drop_labels_on_install)
    InvalidateContourLabels();
  drop_labels_on_install = true;
}

void
GlideConeRenderer::ClearJobClaim() noexcept
{
  /* A failed first build (DEM not ready, empty seeds, GPU context)
     used to leave computed_center/signature set so need_job never
     fired again until the aircraft moved. */
  if (!field.IsValid())
    computed_center = GeoPoint::Invalid();
  computed_signature = 0;
  last_job_attempt = std::chrono::steady_clock::now();
}

void
GlideConeRenderer::InvalidateContourLabels() noexcept
{
  contour_labels.clear();
  label_cache_map_scale = -1;
  label_cache_spacing = 0;
  label_cache_font_h = 0;
  label_cache_screen_size = {};
}

void
GlideConeRenderer::RebuildContourLabels(Canvas &canvas,
                                        const WindowProjection &projection,
                                        const GlideConeSettings &gc,
                                        const MapLook &look) noexcept
{
  contour_labels.clear();
  if (look.overlay.overlay_font == nullptr || field.contour_lines.empty())
    return;

  canvas.Select(*look.overlay.overlay_font);
  LabelBlock label_block;
  label_block.reset();

  /* Place over a padded viewport so modest pans still find labels. */
  const PixelRect screen = projection.GetScreenRect();
  const int pad = int(std::min(screen.GetWidth(), screen.GetHeight()) / 4);
  const PixelRect place_rect{
    screen.left - pad, screen.top - pad,
    screen.right + pad, screen.bottom + pad,
  };

  const unsigned short_side =
    std::min(screen.GetWidth(), screen.GetHeight());
  const unsigned pct = std::clamp(gc.label_spacing, 20u, 100u);
  const double spacing =
    std::max(20.0, double(short_side) * double(pct) / 100.0);
  const double spacing2 = spacing * spacing;
  const double sample_step =
    std::max(8.0, double(look.overlay.overlay_font->GetHeight()));

  std::vector<std::pair<int, PixelPoint>> placed;

  for (const auto &line : field.contour_lines) {
    if (line.points.size() < 2)
      continue;

    ContourLabel proto{};
    proto.level = line.level;
    FormatUserAltitude(double(line.level), proto.text);
    const PixelSize ts = canvas.CalcTextSize(proto.text);
    proto.text_size = ts;
    const double hw = ts.width / 2.0, hh = ts.height / 2.0;
    const double offset_px = std::max(2.0, hh * 1.2);

    double since_sample = sample_step;
    GeoPoint prev_geo = line.points[0];
    PixelPoint prev = projection.GeoToScreen(prev_geo);
    for (std::size_t k = 1; k < line.points.size(); ++k) {
      const GeoPoint cur_geo = line.points[k];
      const PixelPoint cur = projection.GeoToScreen(cur_geo);
      const double dx = cur.x - prev.x, dy = cur.y - prev.y;
      const double seg = std::hypot(dx, dy);
      since_sample += seg;
      if (since_sample < sample_step || seg < 1e-3) {
        prev_geo = cur_geo;
        prev = cur;
        continue;
      }
      since_sample = 0;

      if (cur.x < place_rect.left || cur.x > place_rect.right ||
          cur.y < place_rect.top || cur.y > place_rect.bottom) {
        prev_geo = cur_geo;
        prev = cur;
        continue;
      }

      double a = std::atan2(dy, dx);
      if (std::cos(a) < 0)
        a += M_PI;
      const double ca = std::cos(a), sa = std::sin(a);

      const int lx = cur.x + int(std::lround(sa * offset_px));
      const int ly = cur.y + int(std::lround(-ca * offset_px));

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
      if (too_close) {
        prev_geo = cur_geo;
        prev = cur;
        continue;
      }

      const int aabb_w = int(std::abs(hw * ca) + std::abs(hh * sa)) + 1;
      const int aabb_h = int(std::abs(hw * sa) + std::abs(hh * ca)) + 1;
      const PixelRect rc{lx - aabb_w, ly - aabb_h, lx + aabb_w, ly + aabb_h};
      if (!label_block.check(rc)) {
        prev_geo = cur_geo;
        prev = cur;
        continue;
      }

      placed.emplace_back(line.level, PixelPoint{lx, ly});

      ContourLabel label = proto;
      label.location = cur_geo;
      /* Tip one segment past the anchor so screen tangent stays long. */
      label.along = prev_geo.Interpolate(cur_geo, 2.0);
      contour_labels.push_back(label);

      prev_geo = cur_geo;
      prev = cur;
    }
  }

  label_cache_map_scale = projection.GetMapScale();
  label_cache_spacing = pct;
  label_cache_font_h = look.overlay.overlay_font->GetHeight();
  label_cache_screen_size = projection.GetScreenSize();
  LogFmt("glidecones: label geo-cache rebuild n={} scale={:.0f}",
         contour_labels.size(), label_cache_map_scale);
}

void
GlideConeRenderer::DrawContourLabels(Canvas &canvas,
                                     const WindowProjection &projection,
                                     const MapLook &look) const noexcept
{
  if (look.overlay.overlay_font == nullptr || contour_labels.empty())
    return;

  canvas.Select(*look.overlay.overlay_font);
  canvas.SetBackgroundTransparent();
  const PixelRect screen = projection.GetScreenRect();

  for (const auto &label : contour_labels) {
    if (!label.location.IsValid())
      continue;

    /* Sub-pixel projection: integer GeoToScreen on a short along-tip
       makes atan2/offset shimmer while panning or rotating. */
    const FloatPoint2D pf = projection.GeoToScreenF(label.location);
    if (pf.x < screen.left || pf.x > screen.right ||
        pf.y < screen.top || pf.y > screen.bottom)
      continue;

    const FloatPoint2D qf = label.along.IsValid()
      ? projection.GeoToScreenF(label.along)
      : FloatPoint2D{pf.x + 1.f, pf.y};
    double dx = double(qf.x) - double(pf.x);
    double dy = double(qf.y) - double(pf.y);
    if (std::hypot(dx, dy) < 1e-3) {
      dx = 1;
      dy = 0;
    }

    double a = std::atan2(dy, dx);
    if (std::cos(a) < 0)
      a += M_PI;
    const double ca = std::cos(a), sa = std::sin(a);
    const double offset_px =
      std::max(2.0, label.text_size.height / 2.0 * 1.2);
    const PixelPoint label_pos{
      int(std::lround(double(pf.x) + sa * offset_px)),
      int(std::lround(double(pf.y) - ca * offset_px)),
    };

#ifdef ENABLE_OPENGL
    const Angle angle = Angle::Radians(a);
    canvas.SetTextColor(COLOR_WHITE);
    for (const auto off : {PixelPoint{-1, -1}, PixelPoint{1, -1},
                           PixelPoint{-1, 1}, PixelPoint{1, 1}})
      canvas.DrawText({label_pos.x + off.x, label_pos.y + off.y},
                      label.text, angle);
    canvas.SetTextColor(COLOR_BLACK);
    canvas.DrawText(label_pos, label.text, angle);
#else
    RenderShadowedText(canvas, label.text,
                       {label_pos.x - int(label.text_size.width) / 2,
                        label_pos.y - int(label.text_size.height) / 2},
                       false);
#endif
  }
}

void
GlideConeRenderer::Draw(Canvas &canvas, const WindowProjection &projection,
                        GeoPoint aircraft, bool aircraft_valid,
                        GeoPoint target, bool target_valid,
                        const ComputerSettings &settings,
                        const RasterTerrain *terrain,
                        const Waypoints *waypoints,
                        const WaypointRendererSettings &waypoint_settings,
                        const MapLook &look,
                        GeoPoint pan_probe) noexcept
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
  /* Do not require an existing field — cold start must retry when DEM
     tiles appear after a failed first build. */
  const bool terrain_ready =
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
    waypoints_ready =
      waypoint_serial != computed_waypoint_serial &&
      now - waypoint_debounce_since >= GLIDE_CONE_DEBOUNCE;
  }

  const bool center_moved = have_center &&
    (!computed_center.IsValid() ||
     computed_center.DistanceS(center) > recompute_threshold_m);

  const bool cooled_down =
    now - last_job_attempt >= GLIDE_CONE_DEBOUNCE;
  const bool need_job = have_center && !IsBusy() && cooled_down &&
    (!field.IsValid() || center_moved || sig_ready || terrain_ready ||
     waypoints_ready);

  if (need_job) {
    /* Keep the grid origin until the aircraft/seed actually crosses the
       recompute threshold.  Terrain/settings refreshes used to re-center
       on the live aircraft, which rebuilt contours/labels every tile load
       while flying. */
    const GeoPoint job_center =
      (!center_moved && computed_center.IsValid()) ? computed_center
                                                   : center;

    ++job_generation;
    drop_labels_on_install = center_moved || !computed_center.IsValid();
    gpu_worker.Cancel();
    awaiting_gpu = false;
    (void)gpu_worker.TakeReady();
    last_job_attempt = now;

    LogFmt("glidecones: {} request gen={} via={} lat={:.5f} lon={:.5f} "
           "radius={:.0f}m L/D={:.0f} max_alt={:.0f} cell={:.0f} "
           "why: center={} sig={} terrain={} wpts={} empty={} seeds={}",
           ModeName(mode), job_generation, seed_source,
           job_center.latitude.Degrees(), job_center.longitude.Degrees(),
           radius_m, gc.glide_ratio, gc.max_altitude, gc.cell_size,
           center_moved, sig_ready, terrain_ready, waypoints_ready,
           !field.IsValid(), single_seeds.size());

    GlideConeGridRequest request;
    request.generation = job_generation;
    request.center = job_center;
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
      computed_center = job_center;
      computed_signature = signature;
      computed_terrain_serial = terrain_serial;
      if (mode == GlideConeSettings::Mode::COMBINED)
        computed_waypoint_serial = waypoint_serial;
    } else {
      awaiting_grid = false;
      ClearJobClaim();
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
        else {
          ClearJobClaim();
          LogFmt("glidecones: GPU worker.Request failed gen={}",
                 job_generation);
        }
      } else {
        ClearJobClaim();
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
      if (gpu_ready->hit_iteration_cap)
        Message::AddMessage(
          _("GlideCone compute stopped, raise iteration cap"));
      InstallField(std::move(*gpu_ready->prepared),
                   std::move(gpu_ready->result));
    } else {
      ClearJobClaim();
      LogFmt("glidecones: GPU Finish failed/stale gen={}", job_generation);
    }
  } else if (!gpu_worker.IsBusy()) {
    awaiting_gpu = false;
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
GlideConeRenderer::DrawTraceFrom(Canvas &canvas,
                                 const WindowProjection &projection,
                                 GeoPoint from,
                                 const MapLook &look) const noexcept
{
  if (!from.IsValid() || !field.IsValid())
    return;

  const std::vector<GlideConeField::TraceCell> cells = field.Trace(from);
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
    const auto &a = cells[i - 1];
    const auto &b = cells[i];
    const bool ground = field.IsDownhillGroundSegment(a.x, a.y, b.x, b.y);
    const BulkPixelPoint from_pt = projection.GeoToScreen(
      field.CellToGeo(a.x, a.y));
    const BulkPixelPoint to_pt = projection.GeoToScreen(
      field.CellToGeo(b.x, b.y));

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
    if (!computed_contours ||
        computed_contour_polylines != gc.contour_polylines) {
      field.BuildContours(100, gc.contour_polylines);
      computed_contours = true;
      computed_contour_polylines = gc.contour_polylines;
      InvalidateContourLabels();
    }

    const bool show = projection.GetMapScale() <= gc.contours_min_scale;

    if (show && !field.contour_lines.empty()) {
      const GeoClip clip(projection.GetScreenBounds().Scale(1.1));

      canvas.Select(look.glide_cone_contour_pen);
      for (const auto &line : field.contour_lines)
        DrawClippedGeoPolyline(canvas, projection, clip, line.points);

      if (gc.contour_polylines && look.overlay.overlay_font != nullptr) {
        const unsigned pct = std::clamp(gc.label_spacing, 20u, 100u);
        const unsigned font_h = look.overlay.overlay_font->GetHeight();
        const PixelSize screen_size = projection.GetScreenSize();
        const double map_scale = projection.GetMapScale();

        /* Geo anchors: ignore pan/follow.  Rebuild on zoom / settings. */
        const bool scale_ok = label_cache_map_scale > 0 &&
          std::abs(label_cache_map_scale - map_scale) <=
            label_cache_map_scale * 0.02;
        const bool cache_ok =
          label_cache_map_scale > 0 &&
          scale_ok &&
          label_cache_spacing == pct &&
          label_cache_font_h == font_h &&
          label_cache_screen_size.width == screen_size.width &&
          label_cache_screen_size.height == screen_size.height;

        if (!cache_ok)
          RebuildContourLabels(canvas, projection, gc, look);

        DrawContourLabels(canvas, projection, look);
      }
    }
  } else if (computed_contours) {
    field.contour_lines.clear();
    computed_contours = false;
    InvalidateContourLabels();
  }

  if (aircraft_valid)
    DrawTraceFrom(canvas, projection, aircraft, look);

  if (pan_probe.IsValid() &&
      (!aircraft_valid ||
       pan_probe.DistanceS(aircraft) > GLIDE_CONE_SEED_EPSILON_M))
    DrawTraceFrom(canvas, projection, pan_probe, look);
}
