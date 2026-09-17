// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeOverlay.hpp"
#include "GlideConeField.hpp"
#include "Settings.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Renderer/TextInBox.hpp"
#include "Renderer/LabelBlock.hpp"
#include "Formatter/UserUnits.hpp"
#include "Geo/GeoClip.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Color.hpp"
#include "ui/dim/BulkPoint.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

/** Single-mode / pan-probe epsilon [m] (matches JobController). */
static constexpr double GLIDE_CONE_SEED_EPSILON_M = 50;

/** Debounce zoom/settings-driven contour label rebuilds. */
static constexpr std::chrono::milliseconds GLIDE_CONE_LABEL_DEBOUNCE{100};

/**
 * Clip geo edges, then stroke contiguous visible runs with
 * Canvas::DrawPolyline.
 */
static void
DrawClippedGeoPolylineBatched(Canvas &canvas,
                              const WindowProjection &projection,
                              const GeoClip &clip,
                              std::span<const GeoPoint> points,
                              std::vector<BulkPixelPoint> &run) noexcept
{
  if (points.size() < 2)
    return;

  run.clear();

  const auto flush = [&]() noexcept {
    if (run.size() >= 2)
      canvas.DrawPolyline(run.data(), unsigned(run.size()));
    run.clear();
  };

  for (std::size_t i = 1; i < points.size(); ++i) {
    GeoPoint a = points[i - 1];
    GeoPoint b = points[i];
    if (!a.IsValid() || !b.IsValid()) {
      flush();
      continue;
    }
    if (!clip.ClipLine(a, b)) {
      flush();
      continue;
    }

    const BulkPixelPoint sa = projection.GeoToScreen(a);
    const BulkPixelPoint sb = projection.GeoToScreen(b);

    if (run.empty()) {
      run.push_back(sa);
      run.push_back(sb);
      continue;
    }

    if (run.back().x != sa.x || run.back().y != sa.y) {
      flush();
      run.push_back(sa);
    }
    run.push_back(sb);
  }

  flush();
}

void
GlideConeOverlay::InvalidateLabels() noexcept
{
  contour_labels.clear();
  label_cache_map_scale = -1;
  label_cache_spacing = 0;
  label_cache_font_h = 0;
  label_cache_screen_size = {};
  label_cache_center = GeoPoint::Invalid();
  label_cache_angle = Angle::Zero();
  label_rebuild_pending = false;
  label_rebuild_watch_scale = -1;
  label_rebuild_watch_center = GeoPoint::Invalid();
  label_rebuild_watch_angle = Angle::Zero();
  label_debounce_reason = nullptr;
}

void
GlideConeOverlay::HoldRebase() noexcept
{
  if (labels_hold_rebase)
    return;
  labels_hold_rebase = true;
}

void
GlideConeOverlay::ClearHoldRebase() noexcept
{
  labels_hold_rebase = false;
}

void
GlideConeOverlay::OnFieldInstalled(bool drop_labels) noexcept
{
  if (drop_labels)
    InvalidateLabels();
}

void
GlideConeOverlay::OnContoursReady() noexcept
{
  InvalidateLabels();
  if (labels_hold_rebase) {
    labels_hold_rebase = false;
  }
}

void
GlideConeOverlay::RebuildLabels(Canvas &canvas,
                                const WindowProjection &projection,
                                const GlideConeField &field,
                                const GlideConeSettings &gc,
                                const MapLook &look) noexcept
{
  contour_labels.clear();
  if (look.overlay.overlay_font == nullptr || field.contour_lines.empty())
    return;

  canvas.Select(*look.overlay.overlay_font);
  LabelBlock label_block;
  label_block.reset();

  /* Place over 1.5× the screen so modest pans still find labels. */
  constexpr double LABEL_CACHE_COVER = 1.5;
  const PixelRect screen = projection.GetScreenRect();
  const int pad_x =
    int((LABEL_CACHE_COVER - 1.0) * 0.5 * screen.GetWidth());
  const int pad_y =
    int((LABEL_CACHE_COVER - 1.0) * 0.5 * screen.GetHeight());
  const PixelRect place_rect{
    screen.left - pad_x, screen.top - pad_y,
    screen.right + pad_x, screen.bottom + pad_y,
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
  label_cache_center = projection.GetGeoLocation();
  label_cache_angle = projection.GetScreenAngle();
  label_rebuild_watch_scale = label_cache_map_scale;
  label_rebuild_watch_center = label_cache_center;
  label_rebuild_watch_angle = label_cache_angle;
  label_debounce_reason = nullptr;
  if (labels_hold_rebase) {
    labels_hold_rebase = false;
  }
}

void
GlideConeOverlay::DrawLabels(Canvas &canvas,
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

    /* Integer projection is enough for label placement; sub-pixel
       GeoToScreenF is a perso-only helper. */
    const PixelPoint p = projection.GeoToScreen(label.location);
    if (p.x < screen.left || p.x > screen.right ||
        p.y < screen.top || p.y > screen.bottom)
      continue;

    const PixelPoint q = label.along.IsValid()
      ? projection.GeoToScreen(label.along)
      : PixelPoint{p.x + 1, p.y};
    double dx = double(q.x) - double(p.x);
    double dy = double(q.y) - double(p.y);
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
      int(std::lround(double(p.x) + sa * offset_px)),
      int(std::lround(double(p.y) - ca * offset_px)),
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
GlideConeOverlay::DrawTraceFrom(Canvas &canvas,
                                const WindowProjection &projection,
                                const GlideConeField &field,
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
GlideConeOverlay::DrawTraces(Canvas &canvas,
                             const WindowProjection &projection,
                             const GlideConeField &field,
                             GeoPoint aircraft, bool aircraft_valid,
                             GeoPoint pan_probe,
                             const MapLook &look) const noexcept
{
  if (aircraft_valid)
    DrawTraceFrom(canvas, projection, field, aircraft, look);

  if (pan_probe.IsValid() &&
      (!aircraft_valid ||
       pan_probe.DistanceS(aircraft) > GLIDE_CONE_SEED_EPSILON_M))
    DrawTraceFrom(canvas, projection, field, pan_probe, look);
}

void
GlideConeOverlay::DrawContours(Canvas &canvas,
                               const WindowProjection &projection,
                               const GlideConeField &field,
                               const GlideConeSettings &gc,
                               const MapLook &look,
                               bool hold_labels,
                               [[maybe_unused]] bool awaiting_grid,
                               [[maybe_unused]] bool awaiting_gpu,
                               [[maybe_unused]] bool awaiting_contours) noexcept
{
  const bool show = projection.GetMapScale() <= gc.contours_min_scale;
  if (!show || field.contour_lines.empty()) {
    label_rebuild_pending = false;
    return;
  }

  const GeoClip clip(projection.GetScreenBounds().Scale(1.1));

  canvas.Select(look.glide_cone_contour_pen);
  std::vector<BulkPixelPoint> run;
  run.reserve(64);
  for (const auto &line : field.contour_lines)
    DrawClippedGeoPolylineBatched(canvas, projection, clip,
                                  line.points, run);

  if (look.overlay.overlay_font == nullptr)
    return;

  const unsigned pct = std::clamp(gc.label_spacing, 20u, 100u);
  const unsigned font_h = look.overlay.overlay_font->GetHeight();
  const PixelSize screen_size = projection.GetScreenSize();
  const double map_scale = projection.GetMapScale();
  const GeoPoint view_center = projection.GetGeoLocation();
  const Angle view_angle = projection.GetScreenAngle();

  const bool scale_ok = label_cache_map_scale > 0 &&
    std::abs(label_cache_map_scale - map_scale) <=
      label_cache_map_scale * 0.02;
  const bool settings_ok =
    label_cache_spacing == pct &&
    label_cache_font_h == font_h &&
    label_cache_screen_size.width == screen_size.width &&
    label_cache_screen_size.height == screen_size.height;

  bool view_ok = label_cache_center.IsValid();
  double drift_px = 0;
  double angle_deg = 0;
  if (view_ok) {
    const PixelPoint cached =
      projection.GeoToScreen(label_cache_center);
    const PixelPoint mid = projection.GetScreenRect().GetCenter();
    drift_px = std::hypot(double(cached.x - mid.x),
                          double(cached.y - mid.y));
    const double drift_limit =
      0.25 * double(std::min(screen_size.width, screen_size.height));
    angle_deg =
      std::fabs((view_angle - label_cache_angle).AsDelta().Degrees());
    view_ok = drift_px <= drift_limit && angle_deg <= 5.0;
  }

  const bool cache_ok = label_cache_map_scale > 0 && scale_ok &&
    settings_ok && view_ok;

  const char *dirty =
    label_cache_map_scale < 0 ? "empty" :
    !scale_ok ? "scale" :
    !settings_ok ? "settings" :
    !view_ok ? "view" : "ok";

  if (hold_labels) {
    if (label_rebuild_pending || label_debounce_reason != nullptr) {
      label_debounce_reason = nullptr;
    }
    label_rebuild_pending = false;
  } else if (cache_ok) {
    label_rebuild_pending = false;
    label_debounce_reason = nullptr;
  } else if (label_cache_map_scale < 0) {
    RebuildLabels(canvas, projection, field, gc, look);
    label_rebuild_pending = false;
  } else {
    const auto now = std::chrono::steady_clock::now();
    const bool still_moving =
      label_rebuild_watch_scale > 0 &&
      (std::abs(label_rebuild_watch_scale - map_scale) >
         label_rebuild_watch_scale * 0.005 ||
       (label_rebuild_watch_center.IsValid() &&
        view_center.IsValid() &&
        label_rebuild_watch_center.DistanceS(view_center) > 5.0) ||
       std::fabs((view_angle - label_rebuild_watch_angle)
                   .AsDelta().Degrees()) > 1.0);

    if (!label_rebuild_pending) {
      label_rebuild_pending = true;
      label_rebuild_since = now;
      label_rebuild_watch_scale = map_scale;
      label_rebuild_watch_center = view_center;
      label_rebuild_watch_angle = view_angle;
      label_debounce_reason = dirty;
    } else if (still_moving) {
      label_rebuild_since = now;
      label_rebuild_watch_scale = map_scale;
      label_rebuild_watch_center = view_center;
      label_rebuild_watch_angle = view_angle;
      if (label_debounce_reason != dirty) {
        label_debounce_reason = dirty;
      }
    } else if (now - label_rebuild_since >= GLIDE_CONE_LABEL_DEBOUNCE) {
      RebuildLabels(canvas, projection, field, gc, look);
      label_rebuild_pending = false;
    }
  }

  DrawLabels(canvas, projection, look);
}
