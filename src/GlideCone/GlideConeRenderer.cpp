// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeRenderer.hpp"
#include "GlideConeCompute.hpp"
#include "GlideConeStatus.hpp"
#include "Computer/Settings.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Terrain/RasterMap.hpp"
#include "Terrain/Height.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Terrain/RasterProjection.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "Renderer/TextInBox.hpp"
#include "Renderer/LabelBlock.hpp"
#include "Formatter/UserUnits.hpp"
#include "Screen/Layout.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoClip.hpp"
#include "Math/Angle.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Color.hpp"

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

/** Hard cap on GPU grid cells per side. */
static constexpr unsigned GLIDE_CONE_MAX_DIM = 1024;

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
}

void
GlideConeRenderer::ClearTarget() noexcept
{
  const std::lock_guard lock{mutex};
  pending_valid = false;
  ++pending_generation;
}

[[gnu::pure]]
static std::size_t
SettingsSignature(const GlideConeSettings &s) noexcept
{
  std::size_t h = std::hash<int>{}(int(s.mode));
  h = h * 31 + std::hash<double>{}(s.glide_ratio);
  h = h * 31 + std::hash<double>{}(s.max_altitude);
  h = h * 31 + std::hash<double>{}(s.cell_size);
  h = h * 31 + std::hash<unsigned>{}(s.iteration_cap);
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

bool
GlideConeRenderer::BuildField(GeoPoint center, double radius_m,
                              const std::vector<GeoPoint> &seeds,
                              const ComputerSettings &settings,
                              const RasterTerrain &terrain) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;

  const double ratio = std::clamp(gc.glide_ratio, 1.0, 200.0);
  const double max_alt = std::clamp(gc.max_altitude, 100.0, 10000.0);
  const double desired_cell =
    std::clamp(gc.cell_size, 50.0, 5000.0);

  const RasterTerrain::Lease lease{terrain};
  const RasterMap &map = lease;
  if (!map.IsDefined())
    return false;

  const RasterProjection &proj = map.GetProjection();

  /* DEM lon/lat pixels are not square metres: measure axes separately. */
  double dem_x = map.PixelDistanceX(center, 1);
  double dem_y = map.PixelDistanceY(center, 1);
  if (dem_x < 1)
    dem_x = 1;
  if (dem_y < 1)
    dem_y = 1;

  const double dem_ref = std::sqrt(dem_x * dem_y);
  unsigned pool = std::max(1u, (unsigned)std::lround(desired_cell / dem_ref));

  double cell_x = pool * dem_x;
  double cell_y = pool * dem_y;

  unsigned half_x = std::max(1u, (unsigned)std::lround(radius_m / cell_x));
  unsigned half_y = std::max(1u, (unsigned)std::lround(radius_m / cell_y));

  /* Fit a metric-radius window into the GPU dim cap by coarsening the
     shared DEM pool factor (keeps N×N max-pool alignment). */
  if (half_x > GLIDE_CONE_MAX_DIM / 2 || half_y > GLIDE_CONE_MAX_DIM / 2) {
    const unsigned need_pool_x = std::max(1u, (unsigned)std::lround(
      (radius_m / double(GLIDE_CONE_MAX_DIM / 2)) / dem_x));
    const unsigned need_pool_y = std::max(1u, (unsigned)std::lround(
      (radius_m / double(GLIDE_CONE_MAX_DIM / 2)) / dem_y));
    pool = std::max(pool, std::max(need_pool_x, need_pool_y));
    cell_x = pool * dem_x;
    cell_y = pool * dem_y;
    half_x = std::min(GLIDE_CONE_MAX_DIM / 2,
                      std::max(1u, (unsigned)std::lround(radius_m / cell_x)));
    half_y = std::min(GLIDE_CONE_MAX_DIM / 2,
                      std::max(1u, (unsigned)std::lround(radius_m / cell_y)));
  }

  const unsigned dim_x = 2 * half_x;
  const unsigned dim_y = 2 * half_y;

  const auto c = proj.ProjectCoarse(center);
  const SignedRasterLocation origin{
    c.x - int(half_x * pool),
    c.y - int(half_y * pool),
  };

  const auto nw = proj.UnprojectCoarse(origin);
  const auto se = proj.UnprojectCoarse(SignedRasterLocation{
    origin.x + int(dim_x * pool),
    origin.y + int(dim_y * pool),
  });
  const GeoBounds bounds{nw, se};
  if (!bounds.IsValid())
    return false;

  const double clearance = settings.task.route_planner.safety_height_terrain;
  const double arrival = settings.task.safety_height_arrival;
  const float invalid_elevation = float(max_alt + 10000);

  GlideConeGrid grid;
  grid.width = dim_x;
  grid.height = dim_y;
  grid.cell_size_x_m = cell_x;
  grid.cell_size_y_m = cell_y;
  grid.glide_ratio = ratio;
  grid.max_alt = float(max_alt);
  grid.iteration_cap = gc.iteration_cap;
  grid.elevation.resize(std::size_t(dim_x) * dim_y);

  map.MaxPoolElevation(origin, pool, dim_x, dim_y,
                       grid.elevation.data(), invalid_elevation);
  for (float &e : grid.elevation)
    if (e < invalid_elevation)
      e += float(clearance);

  for (const GeoPoint &seed : seeds) {
    const auto sp = proj.ProjectCoarse(seed);
    const int sx = (sp.x - origin.x) / int(pool);
    const int sy = (sp.y - origin.y) / int(pool);
    if (sx < 0 || sy < 0 ||
        unsigned(sx) >= dim_x || unsigned(sy) >= dim_y)
      continue;

    const double seed_terrain =
      map.GetHeight(seed).ToDouble(0.0, 0.0);
    grid.seeds.push_back({sx, sy, float(seed_terrain + arrival)});
  }

  if (grid.seeds.empty())
    return false;

  GlideConeResult result;
  if (!GlideConeCompute::Run(grid, result))
    return false;

  field.result = std::move(result);
  field.bounds = bounds;
  field.cell_size_m = std::sqrt(cell_x * cell_y);
  field.max_alt = grid.max_alt;
  field.home_x = grid.seeds.front().x;
  field.home_y = grid.seeds.front().y;
  return field.IsValid();
}

void
GlideConeRenderer::Draw(Canvas &canvas, const WindowProjection &projection,
                        GeoPoint aircraft, bool aircraft_valid,
                        GeoPoint target, bool target_valid,
                        const ComputerSettings &settings,
                        const RasterTerrain *terrain,
                        const Waypoints *waypoints,
                        const MapLook &look) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;
  const auto mode = gc.mode;

  const std::size_t signature = SettingsSignature(gc);

  if (mode == GlideConeSettings::Mode::OFF || terrain == nullptr ||
      !GlideConeCompute::Available()) {
    GlideConeStatus::SetInvalid();
    return;
  }

  /* determine the window centre and seeds for the selected mode */
  GeoPoint center = GeoPoint::Invalid();
  double radius_m = gc.WindowRadiusM();
  std::vector<GeoPoint> seeds;
  double recompute_threshold_m = GLIDE_CONE_SEED_EPSILON_M;

  if (mode == GlideConeSettings::Mode::SINGLE) {
    GeoPoint seed;
    bool valid;
    if (target_valid) {
      seed = target;
      valid = true;
    } else {
      const std::lock_guard lock{mutex};
      seed = pending_seed;
      valid = pending_valid;
    }

    if (!valid) {
      field.Clear();
      have_field = false;
      computed_center = GeoPoint::Invalid();
      GlideConeStatus::SetInvalid();
      return;
    }

    center = seed;
    seeds.push_back(seed);
    recompute_threshold_m = GLIDE_CONE_SEED_EPSILON_M;
  } else { // COMBINED
    if (!aircraft_valid || waypoints == nullptr) {
      GlideConeStatus::SetInvalid();
      return;
    }

    center = aircraft;
    recompute_threshold_m = GLIDE_CONE_MAX_OFFSET_FROM_CENTER * radius_m;
  }

  /* debounce parameter (glide ratio, etc.) and terrain-tile changes */
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
  const bool terrain_ready = have_field &&
    terrain_serial != computed_terrain_serial &&
    now - terrain_debounce_since >= GLIDE_CONE_DEBOUNCE;

  const bool center_moved = !computed_center.IsValid() ||
    computed_center.DistanceS(center) > recompute_threshold_m;

  if (!have_field || center_moved || sig_ready || terrain_ready) {
    /* single mode gathers its single seed above; combined gathered its
       seeds only when a recompute was due, so re-gather if needed */
    if (mode == GlideConeSettings::Mode::COMBINED && seeds.empty() &&
        waypoints != nullptr) {
      waypoints->VisitWithinRange(center, radius_m,
                                  [&seeds](const WaypointPtr &wp){
        if (wp->IsLandable())
          seeds.push_back(wp->location);
      });
    }

    have_field = !seeds.empty() &&
      BuildField(center, radius_m, seeds, settings, *terrain);
    computed_center = center;
    computed_signature = signature;
    computed_terrain_serial = terrain_serial;
    computed_contours = false;
  }

  if (!have_field) {
    GlideConeStatus::SetInvalid();
    return;
  }

  if (!aircraft_valid) {
    GlideConeStatus::SetInvalid();
    return;
  }

  /* publish the required altitude at the aircraft for the InfoBox */
  {
    int ax, ay;
    if (field.GeoToCell(aircraft, ax, ay)) {
      const std::size_t index = std::size_t(ay) * field.result.width + ax;
      const float required = field.result.altitudes[index];
      if (required < field.max_alt)
        GlideConeStatus::Set({true, required});
      else
        GlideConeStatus::SetInvalid();
    } else {
      GlideConeStatus::SetInvalid();
    }
  }

  /* altitude contour lines of the reachable area */
  if (gc.contours) {
    if (!computed_contours) {
      field.BuildContours();
      computed_contours = true;
    }

    /* only show once zoomed in past the configured map scale */
    const bool show = projection.GetMapScale() <= gc.contours_min_scale;

    if (show && !field.contour_lines.empty()) {
      const PixelRect screen = projection.GetScreenRect();
      const GeoClip clip(projection.GetScreenBounds().Scale(1.1));

      /* draw the stitched contour polylines */
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

            /* text angle parallel to the line, flipped to read upright */
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
            /* white halo for readability, then black text */
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

  const std::vector<GeoPoint> path = field.Trace(aircraft);
  if (path.size() < 2)
    return;

  canvas.Select(look.glide_cone_pen);
  DrawClippedGeoPolyline(canvas, projection,
                         GeoClip(projection.GetScreenBounds().Scale(1.1)),
                         path);
}
