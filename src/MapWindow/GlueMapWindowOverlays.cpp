// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlueMapWindow.hpp"
#include "Look/MapLook.hpp"
#include "ui/canvas/Icon.hpp"
#include "Language/Language.hpp"
#include "Screen/Layout.hpp"
#include "Task/ProtectedTaskManager.hpp"
#include "Engine/Task/TaskManager.hpp"
#include "Engine/Task/Ordered/OrderedTask.hpp"
#include "Renderer/TextInBox.hpp"
#include "Weather/Rasp/RaspRenderer.hpp"
#include "Formatter/UserUnits.hpp"
#include "Formatter/UserGeoPointFormatter.hpp"
#include "UIState.hpp"
#include "Renderer/FinalGlideBarRenderer.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Terrain/DemOverview.hpp"
#include "util/Macros.hpp"
#include "util/StringAPI.hxx"
#include "Look/GestureLook.hpp"
#include "Renderer/GestureRenderer.hpp"
#include "Input/InputEvents.hpp"
#include "Renderer/MapScaleRenderer.hpp"
#include "Components.hpp"
#include "BackendComponents.hpp"
#include "Replay/Replay.hpp"
#include "MapTimer.hpp"
#include "Look/InfoBoxLook.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Pen.hpp"
#include "ui/canvas/Brush.hpp"
#include "util/StaticString.hxx"
#include "Interface.hpp"
#include "MainWindow.hpp"
#include "PopupMessage.hpp"

#include <algorithm> // for std::clamp()

#if DEBUG_ALL_MAP_OVERLAYS
#include "Engine/Task/Stats/ElementStat.hpp"
#include "Engine/GlideSolvers/GlideResult.hpp"
#include "NMEA/Derived.hpp"
#include "NMEA/MoreData.hpp"

/*
 * Feeds the bar renderers synthetic data so every overlay is on screen
 * at once and each bar reaches its full extent in both directions.
 */
[[gnu::pure]]
static DerivedInfo
DebugFinalGlideData(DerivedInfo calculated) noexcept
{
  /* The renderer bails out without a valid task.  ±468 m drives the
     two bars to opposite ends of the range, so both clipping arrows
     show; mc0 must be valid, or only the upward bar is drawn. */
  ElementStat &total = calculated.task_stats.total;

  calculated.task_stats.task_valid = true;
  total.solution_remaining.validity = GlideResult::Validity::OK;
  total.solution_remaining.altitude_difference = 468;
  total.solution_remaining.pure_glide_altitude_difference = 468;
  total.solution_mc0.validity = GlideResult::Validity::OK;
  total.solution_mc0.altitude_difference = -468;
  total.solution_mc0.pure_glide_altitude_difference = -468;

  return calculated;
}
#endif

void
GlueMapWindow::DrawGesture(Canvas &canvas) const noexcept
{
  if (!gestures.HasPoints())
    return;

  const char *gesture = gestures.GetGesture();
  const bool valid = gesture == nullptr || InputEvents::IsGesture(gesture);

  GestureRenderer::Draw(canvas, gesture_look, gestures.GetPoints(), valid);

  /* name the action which lifting the finger now would trigger */
  const char *label = gesture != nullptr
    ? InputEvents::GetGestureLabel(gesture)
    : nullptr;
  if (label == nullptr)
    return;

  canvas.Select(*look.overlay.overlay_font);

  const PixelRect rc = GetClientRect();

  TextInBoxMode mode;
  mode.shape = LabelShape::PILL;
  mode.align = TextInBoxMode::Alignment::CENTER;
  mode.move_in_view = true;

  TextInBox(canvas, label, {rc.GetCenter().x, rc.top + Layout::Scale(12)},
            mode, rc);
}

void
GlueMapWindow::DrawCrossHairs(Canvas &canvas) const noexcept
{
  if (!render_projection.IsValid())
    return;

  canvas.Select(look.overlay.crosshair_pen);

  const auto center = render_projection.GetScreenOrigin();

  auto FullLength = Layout::FastScale(20);
  auto HalfLength = Layout::FastScale(10);
  auto EdgeLength = Layout::FastScale(30);

  // Edges
  canvas.DrawLine(center.At(FullLength, FullLength), center.At(HalfLength, FullLength));
  canvas.DrawLine(center.At(FullLength, FullLength), center.At(FullLength, HalfLength));

  canvas.DrawLine(center.At(-FullLength, FullLength), center.At(-HalfLength, FullLength));
  canvas.DrawLine(center.At(-FullLength, FullLength), center.At(-FullLength, HalfLength));

  canvas.DrawLine(center.At(-FullLength, -FullLength), center.At(-HalfLength, -FullLength));
  canvas.DrawLine(center.At(-FullLength, -FullLength), center.At(-FullLength, -HalfLength));

  canvas.DrawLine(center.At(FullLength, -FullLength), center.At(FullLength, -HalfLength));
  canvas.DrawLine(center.At(FullLength, -FullLength), center.At(HalfLength, -FullLength));

  // Crosshair
  canvas.Select(look.overlay.crosshair_pen_alias);
  canvas.DrawLine(center.At(0, -EdgeLength), center.At(0, -HalfLength));
  canvas.DrawLine(center.At(0, EdgeLength), center.At(0, HalfLength));

  canvas.DrawLine(center.At(-EdgeLength, 0), center.At(-HalfLength, 0));
  canvas.DrawLine(center.At(EdgeLength, 0), center.At(HalfLength, 0));

}

void
GlueMapWindow::DrawPanInfo(Canvas &canvas) const noexcept
{
  if (!render_projection.IsValid())
    return;

  GeoPoint location = render_projection.GetGeoLocation();

  TextInBoxMode mode;
  mode.shape = LabelShape::OUTLINED;
  mode.align = TextInBoxMode::Alignment::RIGHT;

  const Font &font = *look.overlay.overlay_font;
  canvas.Select(font);

  unsigned padding = Layout::FastScale(4);
  unsigned height = font.GetHeight();

  /* in OVERLAY InfoBox mode, anchor to the non-InfoBox area */
  PixelRect hud_rc(PixelPoint(0, 0), render_projection.GetScreenSize());
  if (content_rect.GetWidth() > 0)
    hud_rc = content_rect;
  PixelPoint p(hud_rc.right - int(padding), hud_rc.top + int(padding));

  if (compass_visible)
    /* don't obscure the north arrow */
    /* TODO: obtain offset from CompassRenderer */
    p.y += Layout::Scale(19) + Layout::FastScale(13);

  if (terrain) {
    TerrainHeight elevation = terrain->GetTerrainHeight(location);
    if (!elevation.IsSpecial()) {
      StaticString<64> elevation_long;
      elevation_long.Format("%s: %s", _("Elevation"),
                            FormatUserAltitude(elevation.GetValue()).c_str());

      TextInBox(canvas, elevation_long, p, mode,
                render_projection.GetScreenSize());

      p.y += height;
    }
  }

  if (GetComputerSettings().glide_cone.IsEnabled() &&
      GetComputerSettings().glide_cone.pan_mode_path) {
    if (const auto required =
          glide_cone_renderer.QueryRequiredAltitude(location)) {
      StaticString<64> glide_cone_long;
      glide_cone_long.Format("%s: %s", "GlideCone",
                             FormatUserAltitude(*required).c_str());
      TextInBox(canvas, glide_cone_long, p, mode,
                render_projection.GetScreenSize());
      p.y += height;
    }
  }

  char buffer[256];
  FormatGeoPoint(location, buffer, ARRAY_SIZE(buffer), '\n');

  char *start = buffer;
  while (true) {
    auto *newline = StringFind(start, '\n');
    if (newline != nullptr)
      *newline = '\0';

    TextInBox(canvas, start, p, mode, render_projection.GetScreenSize());

    p.y += height;

    if (newline == nullptr)
      break;

    start = newline + 1;
  }

  /* RASP field value at the panned location, analogous to the "map
     items at this location" dialog. */
  if (rasp_renderer && rasp_renderer->IsInside(location)) {
    const char *label = rasp_renderer->GetLabel();
    if (label != nullptr && *label != '\0') {
      const auto value = FormatRaspValue(rasp_renderer->GetValueAt(location));

      StaticString<128> rasp_line;
      if (value.empty())
        rasp_line = label;
      else
        rasp_line.Format("%s: %s", label, value.c_str());

      TextInBox(canvas, rasp_line, p, mode,
                render_projection.GetScreenSize());

      p.y += height;
    }
  }
}

void
GlueMapWindow::DrawGPSStatus(Canvas &canvas, const PixelRect &rc,
                             const NMEAInfo &info) const noexcept
{
  const char *txt;
  const MaskedIcon *icon;

  if (!info.alive) {
    icon = &look.no_gps_icon;
    txt = _("GPS not connected");
  } else if (!info.location_available) {
    icon = &look.waiting_for_fix_icon;
    txt = _("GPS waiting for fix");
  } else if (DEBUG_ALL_MAP_OVERLAYS) {
    icon = &look.waiting_for_fix_icon;
    txt = "GPS status";
  } else
    // early exit
    return;

  const Font &font = *look.overlay.overlay_font;
  canvas.Select(font);

  /* DrawMapScale paints the scale bar and the map-title line
     (AUTO / Simulator / REPLAY / …) after this overlay.  Reserve that
     band (and bottom_margin) so the GPS label sits above the title. */
  const int scale_band = (int)font.GetCapitalHeight()
    + (int)Layout::GetTextPadding();
  const int title_band = (int)font.GetHeight()
    + (int)Layout::GetTextPadding();
  const int clear_bottom = rc.bottom - (int)bottom_margin
    - scale_band - title_band - Layout::Scale(2);

  const int row_height = std::max((int)icon->GetSize().height,
                                  (int)font.GetHeight());
  PixelPoint p(rc.left + Layout::FastScale(2),
               clear_bottom - row_height);
  icon->Draw(canvas, p);

  p.x += icon->GetSize().width + Layout::FastScale(4);
  p.y = clear_bottom - (int)font.GetAscentHeight()
    - ((row_height - (int)font.GetHeight()) / 2);

  TextInBoxMode mode;
  mode.shape = LabelShape::ROUNDED_BLACK;

  TextInBox(canvas, txt, p, mode, rc, nullptr);
}

void
GlueMapWindow::DrawFlightMode(Canvas &canvas,
                              const PixelRect &rc) const noexcept
{
  int offset = 0;

  // draw flight mode
  const MaskedIcon *bmp;

  if (Calculated().common_stats.task_type == TaskType::ABORT)
    bmp = &look.abort_mode_icon;
  else if (GetDisplayMode() == DisplayMode::CIRCLING)
    bmp = &look.climb_mode_icon;
  else if (GetDisplayMode() == DisplayMode::FINAL_GLIDE)
    bmp = &look.final_glide_mode_icon;
  else
    bmp = &look.cruise_mode_icon;

  offset += bmp->GetSize().width + Layout::Scale(6);

  bmp->Draw(canvas,
            PixelPoint(rc.right - offset,
                       rc.bottom - bottom_margin - bmp->GetSize().height - Layout::Scale(4)));

  // draw flarm status
  if (!GetMapSettings().show_flarm_alarm_level && !DEBUG_ALL_MAP_OVERLAYS)
    // Don't show indicator when the gauge is indicating the traffic anyway
    return;

  const FlarmStatus &flarm = Basic().flarm.status;
  if (!flarm.available) {
    if (!DEBUG_ALL_MAP_OVERLAYS)
      return;

    bmp = &look.traffic_safe_icon;
  } else
    switch (flarm.alarm_level) {
    case FlarmTraffic::AlarmType::NONE:
      bmp = &look.traffic_safe_icon;
      break;
    case FlarmTraffic::AlarmType::LOW:
    case FlarmTraffic::AlarmType::INFO_ALERT:
      bmp = &look.traffic_warning_icon;
      break;
    case FlarmTraffic::AlarmType::IMPORTANT:
    case FlarmTraffic::AlarmType::URGENT:
      bmp = &look.traffic_alarm_icon;
      break;
    };

  offset += bmp->GetSize().width + Layout::Scale(6);

  bmp->Draw(canvas,
            PixelPoint(rc.right - offset,
                       rc.bottom - bottom_margin - bmp->GetSize().height - Layout::Scale(2)));
}

void
GlueMapWindow::DrawFinalGlide(Canvas &canvas,
                              const PixelRect &rc) const noexcept
{
  const GlideSettings &glide_settings = GetComputerSettings().task.glide;

#if DEBUG_ALL_MAP_OVERLAYS
  final_glide_bar_renderer.Draw(canvas, rc,
                                DebugFinalGlideData(Calculated()),
                                glide_settings, true);
  return;
#else

  if (GetMapSettings().final_glide_bar_display_mode==FinalGlideBarDisplayMode::OFF)
    return;

  if (GetMapSettings().final_glide_bar_display_mode==FinalGlideBarDisplayMode::AUTO) {
    const TaskStats &task_stats = Calculated().task_stats;
    const ElementStat &total = task_stats.total;
    const GlideResult &solution = total.solution_remaining;
    const GlideResult &solution_mc0 = total.solution_mc0;

    if (!task_stats.task_valid || !solution.IsOk() || !solution_mc0.IsDefined())
      return;

    if (solution_mc0.SelectAltitudeDifference(glide_settings) < -1000 &&
        solution.SelectAltitudeDifference(glide_settings) < -1000)
      return;
  }

  final_glide_bar_renderer.Draw(canvas, rc, Calculated(),
                                glide_settings,
                                GetMapSettings().final_glide_bar_mc0_enabled);
#endif
}

void
GlueMapWindow::DrawVario(Canvas &canvas, const PixelRect &rc) const noexcept
{
  const GlidePolar &polar = GetComputerSettings().polar.glide_polar_task;

#if DEBUG_ALL_MAP_OVERLAYS
  /* gross and average vario at opposite ends of the ±5 m/s range, so
     both bars reach their full extent in both directions */
  MoreData basic = Basic();
  DerivedInfo calculated = Calculated();
  basic.brutto_vario = basic.filtered_brutto_vario = 5;
  calculated.average = -5;

  vario_bar_renderer.Draw(canvas, rc, basic, calculated, polar, true);
#else
  if (!GetMapSettings().vario_bar_enabled)
   return;

  vario_bar_renderer.Draw(canvas, rc, Basic(), Calculated(),
                                polar,
                                true); //NOTE: AVG enabled for now, make it configurable ;
#endif
}

void
GlueMapWindow::SetBottomMargin(unsigned margin) noexcept
{
  if (margin == bottom_margin)
    /* no change, don't redraw */
    return;

  bottom_margin = margin;
  QuickRedraw();
}

void
GlueMapWindow::SetTopRightMargin(unsigned margin) noexcept
{
  if (margin == top_right_margin)
    /* no change, don't redraw */
    return;

  top_right_margin = margin;
  QuickRedraw();
}

void
GlueMapWindow::SetMenuVisible(bool visible) noexcept
{
  if (visible == menu_visible)
    /* no change, don't redraw */
    return;

  menu_visible = visible;
  QuickRedraw();
}

void
GlueMapWindow::SetContentRect(PixelRect rc) noexcept
{
  content_rect = rc;
  QuickRedraw();
}

void
GlueMapWindow::SetBottomMarginFactor(unsigned margin_factor) noexcept
{
  if (follow_mode != FOLLOW_PAN || Layout::landscape) {
    /* only apply bottom margin in portrait pan mode where
       overlay buttons cover the bottom of the screen */
    SetBottomMargin(0);
    return;
  }

  if (margin_factor == 0) {
    SetBottomMargin(0);
    return;
  }

  PixelRect parent_rect = GetParentClientRect();
  unsigned screen_height = parent_rect.GetHeight();

  SetBottomMargin(screen_height / margin_factor);
}

void
GlueMapWindow::DrawMapScale(Canvas &canvas, const PixelRect &rc,
                            const MapWindowProjection &projection) const noexcept
{

  PixelRect scale_pos(rc.left, rc.top, rc.right, rc.bottom - bottom_margin);

  unsigned contour_spacing_m = 0;
  const auto &terrain_settings = GetMapSettings().terrain;
  if (projection.IsValid() &&
      terrain_settings.enable && terrain_settings.contours != Contours::OFF &&
      background.AreContoursVisible())
    contour_spacing_m = background.GetContourSpacing();

  unsigned dem_lod = 0;
  if (projection.IsValid() && terrain_settings.enable &&
      terrain != nullptr) {
    const RasterTerrain::Lease map(*terrain);
    switch (map->GetDisplayLod()) {
    case DemOverview::Lod::FINE:
      dem_lod = 1;
      break;
    case DemOverview::Lod::MEDIUM:
      dem_lod = 2;
      break;
    case DemOverview::Lod::COARSE:
      dem_lod = 3;
      break;
    }
  }

  RenderMapScale(canvas, projection, scale_pos, look.overlay,
                 contour_spacing_m, dem_lod);

  if (!projection.IsValid())
    return;

  StaticString<256> buffer;

  buffer.clear();

  if (GetMapSettings().auto_zoom_enabled)
    buffer = "AUTO ";

  switch (follow_mode) {
  case FOLLOW_SELF:
    break;

  case FOLLOW_PAN:
    buffer += "PAN ";
    break;
  }

  const UIState &ui_state = GetUIState();
  if (Basic().gps.replay) {
    if (backend_components != nullptr &&
        backend_components->replay != nullptr)
      buffer.AppendFormat(_("REPLAY %.0fx "),
                          backend_components->replay->GetTimeScale());
    else
      buffer += _("REPLAY ");
  } else if (Basic().gps.simulator) {
    buffer += _("Simulator");
    buffer += " ";
  }

  if (!ui_state.map_scale_page_title.empty()) {
    buffer += "| ";
    buffer += ui_state.map_scale_page_title;
    buffer += " ";
  } else if (ui_state.auxiliary_enabled) {
    buffer += ui_state.panel_name;
    buffer += " ";
  }

  if (GetComputerSettings().polar.ballast_timer_active)
    buffer.AppendFormat(
        "BALLAST %d LITERS ",
        (int)GetComputerSettings().polar.glide_polar_task.GetBallastLitres());

  if (buffer.empty() && DEBUG_ALL_MAP_OVERLAYS)
    buffer = "Map title";

  if (!buffer.empty()) {

    const Font &font = *look.overlay.overlay_font;
    canvas.Select(font);
    const int height = font.GetCapitalHeight()
        + Layout::GetTextPadding();

    TextInBoxMode mode;
    mode.vertical_position = TextInBoxMode::VerticalPosition::ABOVE;
    mode.shape = LabelShape::OUTLINED;

    TextInBox(canvas, buffer, {scale_pos.left, scale_pos.bottom - height},
              mode, rc, nullptr);
  }
}

PixelRect
GlueMapWindow::GetMapTimerRect(const PixelRect &rc) const noexcept
{
  const Font &font = info_box_look.value_font;
  const auto elapsed = MapTimer::GetElapsed();
  const unsigned total_s = unsigned(std::max<std::chrono::seconds::rep>(
    elapsed.count(), 0));
  const unsigned minutes = total_s / 60;
  const unsigned seconds = total_s % 60;

  StaticString<16> text;
  text.Format("%u:%02u", minutes, seconds);

  const PixelSize text_size = font.TextSize(text.c_str());
  const unsigned pad_x = Layout::GetTextPadding() * 3;
  const unsigned pad_y = Layout::GetTextPadding() * 2;
  const unsigned width = text_size.width + pad_x * 2;
  const unsigned height = text_size.height + pad_y * 2;
  const int left = rc.GetCenter().x - int(width) / 2;

  /* Top of the map by default; drop below a status popup when one is
     covering the top area. */
  int top = rc.top + int(Layout::Scale(8));
  if (CommonInterface::main_window != nullptr) {
    const PopupMessage *popup = CommonInterface::main_window->popup;
    if (popup != nullptr && popup->IsVisible()) {
      const PixelRect map_pos = GetPosition();
      const PixelRect popup_pos = popup->GetPosition();
      const int popup_top = popup_pos.top - map_pos.top;
      const int popup_bottom = popup_pos.bottom - map_pos.top;
      if (popup_top < top + int(height) + int(Layout::Scale(8)))
        top = std::max(top, popup_bottom + int(Layout::Scale(8)));
    }
  }

  return PixelRect{{left, top}, PixelSize{width, height}};
}

bool
GlueMapWindow::MapTimerHitTest(PixelPoint p) const noexcept
{
  if (!MapTimer::IsVisible())
    return false;

  const PixelRect pill = GetMapTimerRect(GetHudLayout().content);
  return pill.GetWidth() > 0 && pill.Contains(p);
}

void
GlueMapWindow::DrawMapTimer(Canvas &canvas, const PixelRect &rc) const noexcept
{
  if (!MapTimer::IsVisible())
    return;

  const Font &font = info_box_look.value_font;
  canvas.Select(font);

  const auto elapsed = MapTimer::GetElapsed();
  const unsigned total_s = unsigned(std::max<std::chrono::seconds::rep>(
    elapsed.count(), 0));
  const unsigned minutes = total_s / 60;
  const unsigned seconds = total_s % 60;

  StaticString<16> text;
  text.Format("%u:%02u", minutes, seconds);

  const PixelRect pill = GetMapTimerRect(rc);
  if (pill.GetWidth() <= 0 || pill.GetHeight() <= 0)
    return;

  /* Follow the InfoBox (navbox) theme: light = white fill / black text;
     dark = black fill / white text.  A contrasting border while the
     timer is running shows that it is active. */
  const bool running = MapTimer::IsRunning();
  const Color fill = info_box_look.background_color;
  const Color text_color = info_box_look.value.fg_color;
  const Color border_color = info_box_look.inverse
    ? COLOR_WHITE
    : COLOR_BLACK;

  canvas.Select(Brush{fill});
  if (running)
    canvas.Select(Pen{Layout::ScalePenWidth(2), border_color});
  else
    canvas.SelectNullPen();
  canvas.DrawRoundRectangle(pill, PixelSize{pill.GetHeight()});

  canvas.SetTextColor(text_color);
  canvas.SetBackgroundTransparent();
  const PixelSize text_size = font.TextSize(text.c_str());
  canvas.DrawText(pill.GetCenter() - text_size / 2u, text.c_str());
}

void
GlueMapWindow::DrawThermalEstimate(Canvas &canvas) const noexcept
{
  if (!GetMapSettings().show_thermal_marker) {
    /* Ownship markers off; still draw Cloud / TIM via the base path. */
    MapWindow::DrawThermalEstimate(canvas);
    return;
  }

  if (InCirclingMode() && IsNearSelf()) {
    // in circling mode, draw thermal at actual estimated location
    const MapWindowProjection &projection = render_projection;
    const ThermalLocatorInfo &thermal_locator = Calculated().thermal_locator;
    if (thermal_locator.estimate_valid) {
      if (auto p = projection.GeoToScreenIfVisible(thermal_locator.estimate_location)) {
        look.thermal_source_icon.Draw(canvas, *p);
      }
    }
  } else {
    MapWindow::DrawThermalEstimate(canvas);
  }
}

void
GlueMapWindow::RenderTrail(Canvas &canvas,
                           const PixelPoint aircraft_pos) noexcept
{
  TimeStamp min_time;
  switch(GetMapSettings().trail.length) {
  case TrailSettings::Length::OFF:
    return;
  case TrailSettings::Length::LONG:
    min_time = std::max(Basic().time - std::chrono::hours{1}, TimeStamp{});
    break;
  case TrailSettings::Length::SHORT:
    min_time = std::max(Basic().time - std::chrono::minutes{10}, TimeStamp{});
    break;
  case TrailSettings::Length::FULL:
  default:
    min_time = {}; // full
    break;
  }

  /* Trail drift is for thermal centering at near zoom.  At overview
     scales a Full trail shifted by hours of wind looks like a wrong
     ground track (#2835, #709).  GetMapScale 2000 ≈ 16 km short edge. */
  static constexpr double TRAIL_DRIFT_MAX_MAP_SCALE = 2000;

  const bool enable_traildrift =
    GetMapSettings().trail.wind_drift_enabled &&
    InCirclingMode() &&
    render_projection.GetMapScale() <= TRAIL_DRIFT_MAX_MAP_SCALE;

  DrawTrail(canvas, aircraft_pos, min_time, enable_traildrift);
}

void
GlueMapWindow::RenderTrackBearing(Canvas &canvas,
                                  const PixelPoint aircraft_pos) noexcept
{
  DrawTrackBearing(canvas, aircraft_pos, InCirclingMode());
}

void
GlueMapWindow::DrawThermalBand(Canvas &canvas,
                               const PixelRect &rc) const noexcept
{
  if (Calculated().task_stats.total.solution_remaining.IsOk() &&
      Calculated().task_stats.total.solution_remaining.altitude_difference > 50
      && GetDisplayMode() == DisplayMode::FINAL_GLIDE)
    return;

  PixelRect tb_rect;
  tb_rect.left = rc.left;
  tb_rect.right = rc.left+Layout::Scale(25);
  tb_rect.top = rc.top + Layout::Scale(2);
  tb_rect.bottom = rc.top + (rc.bottom-rc.top)/5 - Layout::Scale(2);

  const ThermalBandRenderer &renderer = thermal_band_renderer;
  if (task != nullptr) {
    ProtectedTaskManager::Lease task_manager(*task);
    renderer.DrawThermalBand(Basic(),
                             Calculated(),
                             GetComputerSettings(),
                             canvas,
                             tb_rect,
                             GetComputerSettings().task,
                             true,
                             &task_manager->GetOrderedTask().GetOrderedTaskSettings());
  } else {
    renderer.DrawThermalBand(Basic(),
                             Calculated(),
                             GetComputerSettings(),
                             canvas,
                             tb_rect,
                             GetComputerSettings().task,
                             true);
  }
}

void
GlueMapWindow::DrawStallRatio(Canvas &canvas,
                              const PixelRect &rc) const noexcept
{
  // JMW experimental, display stall sensor
  if (!Basic().stall_ratio_available && !DEBUG_ALL_MAP_OVERLAYS)
    return;

  const auto s = DEBUG_ALL_MAP_OVERLAYS
    ? 0.5
    : std::clamp(Basic().stall_ratio, 0., 1.);
  const int m = rc.GetHeight() * s * s;

  const auto p = rc.GetBottomRight();

  canvas.SelectBlackPen();
  canvas.DrawLine(p.At(-1, -m), p.At(-11, -m));
}
