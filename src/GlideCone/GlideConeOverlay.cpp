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
#include "ui/canvas/Brush.hpp"
#include "ui/canvas/Color.hpp"
#include "ui/dim/BulkPoint.hpp"
#include "Screen/Layout.hpp"
#include "Asset.hpp"
#include "LogFile.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <fmt/format.h>

/** Single-mode / pan-probe epsilon [m] (matches JobController). */
static constexpr double GLIDE_CONE_SEED_EPSILON_M = 50;

/** Debounce zoom/settings-driven contour label rebuilds. */
static constexpr std::chrono::milliseconds GLIDE_CONE_LABEL_DEBOUNCE{100};

/**
 * Stored field altitude [m MSL] at a cell: required height on air,
 * terrain on ground (same value written into altitudes[] by compute).
 */
[[gnu::pure]]
static std::optional<double>
CellStoredAltitude(const GlideConeField &field, int x, int y) noexcept
{
  const auto &r = field.result;
  if (!r.IsValid() || x < 0 || y < 0 ||
      unsigned(x) >= r.width || unsigned(y) >= r.height)
    return std::nullopt;

  return double(r.altitudes[std::size_t(y) * r.width + std::size_t(x)]);
}

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
GlideConeOverlay::DrawTraceFrom(Canvas &canvas,
                                const WindowProjection &projection,
                                const GlideConeField &field,
                                GeoPoint from,
                                std::optional<double> start_altitude,
                                bool pan_path,
                                const MapLook &look,
                                std::vector<PixelPoint> &critical) const noexcept
{
  if (!from.IsValid() || !field.IsValid())
    return;

  const std::vector<GlideConeField::TraceCell> cells = field.Trace(from);
  if (cells.size() < 2)
    return;

  enum class SegStyle : uint8_t {
    Pink,
    Ground,
    BelowRed,
  };

  std::vector<BulkPixelPoint> run;
  SegStyle run_style = SegStyle::Pink;
  const auto flush = [&]() {
    if (run.size() < 2)
      return;
    if (run_style == SegStyle::BelowRed) {
      canvas.Select(look.glide_cone_critical_border_pen);
      canvas.DrawPolyline(run.data(), unsigned(run.size()));
      canvas.Select(look.glide_cone_critical_pen);
      canvas.DrawPolyline(run.data(), unsigned(run.size()));
    } else if (run_style == SegStyle::Ground) {
      canvas.Select(look.glide_cone_ground_border_pen);
      for (std::size_t i = 1; i < run.size(); ++i)
        canvas.DrawLine(run[i - 1], run[i]);
      canvas.Select(look.glide_cone_ground_pen);
      for (std::size_t i = 1; i < run.size(); ++i)
        canvas.DrawLine(run[i - 1], run[i]);
    } else {
      canvas.Select(look.glide_cone_border_pen);
      canvas.DrawPolyline(run.data(), unsigned(run.size()));
      canvas.Select(look.glide_cone_pen);
      canvas.DrawPolyline(run.data(), unsigned(run.size()));
    }
  };

  const double safety_ld = field.glide_ratio * 0.8;
  const auto proof_at_from =
    field.RidgeSoaringProofGlideConeAltitude(from);
  const double start_alt = start_altitude.value_or(-1);
  const double margin = (start_altitude && proof_at_from)
    ? (*start_altitude - *proof_at_from)
    : 0;

  /*
   * Below cone (aircraft only): find the first path segment whose
   * geometric L/D is steeper than the safety L/D.  Paint the prefix
   * red, mark that point (unless it is the glider cell), then resume
   * normal 80% budget detection from there.
   */
  std::size_t below_crit_seg = 0;
  const bool below_cone = !pan_path && start_altitude && proof_at_from &&
    *start_altitude < *proof_at_from && safety_ld > 0;
  if (below_cone) {
    for (std::size_t i = 1; i < cells.size(); ++i) {
      const auto &a = cells[i - 1];
      const auto &b = cells[i];
      const GeoPoint geo_from =
        (i == 1) ? from : field.CellToGeo(a.x, a.y);
      const GeoPoint geo_to = field.CellToGeo(b.x, b.y);
      const auto h_from = CellStoredAltitude(field, a.x, a.y);
      const auto h_to = CellStoredAltitude(field, b.x, b.y);
      if (!geo_from.IsValid() || !geo_to.IsValid() ||
          !h_from || !h_to || *h_from <= *h_to)
        continue;

      const double dist = geo_from.DistanceS(geo_to);
      if (dist <= 0)
        continue;

      if (dist / (*h_from - *h_to) < safety_ld) {
        below_crit_seg = i;
        break;
      }
    }
  }

  double altitude = 0;
  bool track_alt = false;
  bool await_first_contact = false;
  const char *mode = "none";

  if (below_crit_seg > 0) {
    const auto &ca = cells[below_crit_seg - 1];
    const GeoPoint crit_geo =
      (below_crit_seg == 1) ? from : field.CellToGeo(ca.x, ca.y);
    /* Disc budget uses stored field height (terrain on ground). */
    if (const auto h = CellStoredAltitude(field, ca.x, ca.y);
        h && safety_ld > 0) {
      altitude = *h;
      track_alt = true;
    }
    if (below_crit_seg > 1 && crit_geo.IsValid()) {
      const auto pt = projection.GeoToScreen(crit_geo);
      critical.emplace_back(pt.x, pt.y);
    }
    mode = below_crit_seg > 1 ? "below-steep+disc" : "below-steep-glider";
  } else if (pan_path && field.IsGroundAt(cells[0].x, cells[0].y)) {
    if (const auto h = CellStoredAltitude(field, cells[0].x, cells[0].y);
        h && safety_ld > 0) {
      altitude = *h;
      track_alt = true;
    }
    mode = "pan-start-ground";
  } else if (pan_path) {
    await_first_contact = true;
    mode = "pan-await-first";
  } else if (start_altitude && safety_ld > 0) {
    altitude = *start_altitude;
    track_alt = true;
    mode = "budget-80";
  }

  /* Debug dump (rate-limited): only when the path decision changes. */
  static bool last_pan = false;
  static double last_start_alt = -1e9;
  static double last_margin = -1e9;
  static std::size_t last_cells = 0;
  static std::size_t last_below = 0;
  static int last_c0x = -1, last_c0y = -1;
  static auto last_log = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  const bool time_ok =
    last_log == std::chrono::steady_clock::time_point{} ||
    now - last_log >= std::chrono::seconds{1};
  const bool changed =
    pan_path != last_pan ||
    cells.size() != last_cells ||
    below_crit_seg != last_below ||
    cells[0].x != last_c0x || cells[0].y != last_c0y ||
    std::fabs(start_alt - last_start_alt) > 0.5 ||
    std::fabs(margin - last_margin) > 0.5;
  const bool do_log = time_ok && changed;

  if (do_log) {
    LogFmt("glideconepath: --- {} cells={} ld={:.1f} safety={:.1f} "
           "start_alt={:.1f} proof_at_start={} margin={:.1f} below={} "
           "below_crit_seg={} mode={} track_alt={}",
           pan_path ? "pan" : "aircraft",
           cells.size(), field.glide_ratio, safety_ld, start_alt,
           proof_at_from ? fmt::format("{:.1f}", *proof_at_from)
                         : std::string("n/a"),
           margin, below_cone, below_crit_seg, mode, track_alt);

    for (std::size_t ci = 0; ci < cells.size(); ++ci) {
      const auto &c = cells[ci];
      const bool g = field.IsGroundAt(c.x, c.y);
      const auto ph = CellStoredAltitude(field, c.x, c.y);
      const GeoPoint geo = (ci == 0) ? from : field.CellToGeo(c.x, c.y);
      const auto proof = geo.IsValid()
        ? field.RidgeSoaringProofGlideConeAltitude(geo)
        : std::nullopt;
      LogFmt("glideconepath: cell[{}] ({},{}) {} stored={} proof={}",
             ci, c.x, c.y, g ? "ground" : "air",
             ph ? fmt::format("{:.1f}", *ph) : std::string("n/a"),
             proof ? fmt::format("{:.1f}", *proof) : std::string("n/a"));
    }
  }

  for (std::size_t i = 1; i < cells.size(); ++i) {
    const auto &a = cells[i - 1];
    const auto &b = cells[i];
    const bool downhill_ground =
      field.IsDownhillGroundSegment(a.x, a.y, b.x, b.y);
    const bool a_ground = field.IsGroundAt(a.x, a.y);
    const bool b_ground = field.IsGroundAt(b.x, b.y);
    /* First vertex is the real start (aircraft / pan probe), not the
       start-cell centre — cells can be hundreds of metres wide. */
    const GeoPoint geo_from = (i == 1) ? from : field.CellToGeo(a.x, a.y);
    const GeoPoint geo_to = field.CellToGeo(b.x, b.y);
    if (!geo_from.IsValid() || !geo_to.IsValid())
      continue;

    const bool in_below_prefix = below_crit_seg > 0 && i <= below_crit_seg;
    const bool past_below_resume =
      below_crit_seg == 0 || i >= below_crit_seg;

    const double dist = geo_from.DistanceS(geo_to);
    const double pretend_before = altitude;
    if (past_below_resume && track_alt)
      altitude -= dist / safety_ld;

    const bool air_to_ground = !a_ground && b_ground;
    const auto h_from = CellStoredAltitude(field, a.x, a.y);
    const auto h_to = CellStoredAltitude(field, b.x, b.y);
    const double seg_ld = (h_from && h_to && *h_from > *h_to && dist > 0)
      ? dist / (*h_from - *h_to)
      : -1;

    /*
     * Red discs compare pretend altitude to stored field height
     * (terrain on ground), not RidgeSoaringProofGlideConeAltitude
     * (InfoBox / pan label).
     */
    const char *disc_why = "none";
    bool disc_here = false;
    if (past_below_resume && (track_alt || await_first_contact) &&
        air_to_ground) {
      if (h_to) {
        if (await_first_contact) {
          const auto pt = projection.GeoToScreen(geo_to);
          critical.emplace_back(pt.x, pt.y);
          altitude = *h_to;
          track_alt = safety_ld > 0;
          await_first_contact = false;
          disc_here = true;
          disc_why = "pan-first-contact";
        } else if (altitude < *h_to) {
          const auto pt = projection.GeoToScreen(geo_to);
          critical.emplace_back(pt.x, pt.y);
          altitude = *h_to;
          disc_here = true;
          disc_why = "pretend<path_h";
        } else {
          disc_why = "pretend>=path_h";
        }
      } else {
        disc_why = "no-path_h";
      }
    } else if (air_to_ground) {
      disc_why = past_below_resume ? "not-tracking" : "before-resume";
    }

    if (do_log && below_crit_seg > 1 && i == below_crit_seg)
      LogFmt("glideconepath: seg[{}] BELOW-STEEP disc_at_begin={} "
             "(glider_cell={})",
             i, true, false);

    const SegStyle style = in_below_prefix
      ? SegStyle::BelowRed
      : (downhill_ground ? SegStyle::Ground : SegStyle::Pink);
    const char *style_name = style == SegStyle::BelowRed
      ? "red"
      : (style == SegStyle::Ground ? "black" : "pink");

    if (do_log) {
      LogFmt("glideconepath: seg[{}] ({},{}){}->({},{}){} "
             "dist={:.0f} seg_ld={:.1f} style={} downhill={} "
             "a2g={} pretend={:.1f}->{:.1f} path_h={} disc={} ({})",
             i, a.x, a.y, a_ground ? "G" : "A",
             b.x, b.y, b_ground ? "G" : "A",
             dist, seg_ld, style_name, downhill_ground, air_to_ground,
             pretend_before, altitude,
             h_to ? fmt::format("{:.1f}", *h_to) : std::string("n/a"),
             disc_here, disc_why);
    }

    const BulkPixelPoint from_pt = projection.GeoToScreen(geo_from);
    const BulkPixelPoint to_pt = projection.GeoToScreen(geo_to);

    if (run.empty()) {
      run_style = style;
      run.push_back(from_pt);
      run.push_back(to_pt);
      continue;
    }

    if (style == run_style) {
      run.push_back(to_pt);
      continue;
    }

    flush();
    run.clear();
    run_style = style;
    run.push_back(from_pt);
    run.push_back(to_pt);
  }
  flush();

  if (do_log) {
    if (below_crit_seg > 1)
      LogFmt("glideconepath: disc[below-steep] at begin of seg[{}]",
             below_crit_seg);
    LogFmt("glideconepath: discs_drawn={}", critical.size());
    last_pan = pan_path;
    last_start_alt = start_alt;
    last_margin = margin;
    last_cells = cells.size();
    last_below = below_crit_seg;
    last_c0x = cells[0].x;
    last_c0y = cells[0].y;
    last_log = now;
  }
}

std::vector<PixelPoint>
GlideConeOverlay::DrawTraces(Canvas &canvas,
                             const WindowProjection &projection,
                             const GlideConeField &field,
                             GeoPoint aircraft, bool aircraft_valid,
                             std::optional<double> aircraft_altitude,
                             GeoPoint pan_probe,
                             std::optional<double> pan_altitude,
                             const MapLook &look) const noexcept
{
  std::vector<PixelPoint> critical;

  if (aircraft_valid)
    DrawTraceFrom(canvas, projection, field, aircraft, aircraft_altitude,
                  false, look, critical);

  if (pan_probe.IsValid() &&
      (!aircraft_valid ||
       pan_probe.DistanceS(aircraft) > GLIDE_CONE_SEED_EPSILON_M))
    DrawTraceFrom(canvas, projection, field, pan_probe, pan_altitude,
                  true, look, critical);

  return critical;
}

void
GlideConeOverlay::DrawCriticalDiscs(Canvas &canvas,
                                    std::span<const PixelPoint> discs) const noexcept
{
  if (discs.empty())
    return;

  const unsigned radius = std::max(3u, unsigned(Layout::Scale(5)));
  const Brush brush(HasColors() ? COLOR_RED : COLOR_BLACK);
  canvas.Select(brush);
  canvas.SelectNullPen();
  for (const auto &pt : discs)
    canvas.DrawCircle(pt, radius);
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
