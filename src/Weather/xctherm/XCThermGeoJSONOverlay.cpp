// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "XCThermGeoJSONOverlay.hpp"
#include "lib/fmt/ToBuffer.hxx"

#include <cstring>
#include <string>
#include "XCThermAPI.hpp"

#include "Geo/GeoBounds.hpp"
#include "Look/Colors.hpp"
#include "Projection/WindowProjection.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Color.hpp"
#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Scope.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

static bool
FormatLocalHourMinute(std::time_t t, char *buffer, std::size_t size) noexcept
{
  std::tm tm_buf{};
#ifdef _WIN32
  return localtime_s(&tm_buf, &t) == 0 &&
    std::strftime(buffer, size, "%H:%M", &tm_buf) > 0;
#else
  return localtime_r(&t, &tm_buf) != nullptr &&
    std::strftime(buffer, size, "%H:%M", &tm_buf) > 0;
#endif
}

void
XCThermGeoJSONOverlay::SetForecast(
    XCThermGeoJSON::ForecastLayer &&_forecast,
    const char *_label,
    const char *_parameter,
    unsigned _forecast_utc) noexcept
{
  const std::lock_guard lock{mutex};
  forecast = std::move(_forecast);
  label = _label != nullptr ? _label : "XC Therm";
  parameter = _parameter != nullptr ? _parameter : "";
  forecast_utc = _forecast_utc;
}

void
XCThermGeoJSONOverlay::SetOpacityPercent(unsigned percent) noexcept
{
  if (percent < 50)
    percent = 50;
  else if (percent > 100)
    percent = 100;

  const std::lock_guard lock{mutex};
  opacity_percent = percent;
}

bool
XCThermGeoJSONOverlay::HasData() const noexcept
{
  const std::lock_guard lock{mutex};
  return !forecast.IsEmpty();
}

bool
XCThermGeoJSONOverlay::MatchesForecast(const char *parameter,
                                        unsigned utc_hour) const noexcept
{
  if (parameter == nullptr || parameter[0] == '\0')
    return false;

  const std::lock_guard lock{mutex};
  return !forecast.IsEmpty() && forecast_utc == utc_hour &&
    this->parameter == parameter;
}

XCThermGeoJSON::ForecastLayer
XCThermGeoJSONOverlay::TakeForecast(std::string &out_label,
                                    std::string &out_parameter,
                                    unsigned &out_forecast_utc) noexcept
{
  const std::lock_guard lock{mutex};
  out_label = std::move(label);
  out_parameter = std::move(parameter);
  out_forecast_utc = forecast_utc;
  forecast_utc = 0;
  return std::move(forecast);
}

bool
XCThermGeoJSONOverlay::GetClimbAt(GeoPoint p, double &out_min_ms,
                                  double &out_max_ms) const noexcept
{
  const std::lock_guard lock{mutex};
  /* Pure geometry lives in XCThermGeoJSON::FindBandAtPoint so it can be
     unit-tested without dragging in the overlay's UI dependencies. */
  return XCThermGeoJSON::FindBandAtPoint(forecast, p, out_min_ms, out_max_ms);
}

bool
XCThermGeoJSONOverlay::FormatPointInfo(const GeoPoint &p, char *buffer,
                                       std::size_t size) const noexcept
{
  if (buffer == nullptr || size == 0)
    return false;

  /* Snapshot the metadata we need under the lock, then format without
     holding it (snprintf + API call must not nest the mutex). */
  std::string label_copy, parameter_copy;
  unsigned hour;
  {
    const std::lock_guard lock{mutex};
    if (forecast.IsEmpty())
      return false;
    label_copy = label;
    parameter_copy = parameter;
    hour = forecast_utc;
  }

  /* Climb value at the tapped location.
     The source data is contoured into bands server-side, so the finest
     value available is the band the point falls in; we report its
     midpoint as the representative number. The neutral band
     (−0.2…+0.2 m/s) is dropped at parse time, so a point inside no band
     is neutral → 0.0. Open-ended edge bands carry a ±1000 sentinel
     bound; for those the finite edge is the only meaningful figure. */
  double min_ms = 0, max_ms = 0;
  std::string climb;
  if (GetClimbAt(p, min_ms, max_ms)) {
    double value;
    if (min_ms <= -100.0)
      value = max_ms;
    else if (max_ms >= 100.0)
      value = min_ms;
    else
      value = (min_ms + max_ms) / 2;
    climb = std::string(FmtBuffer<32>("{:+.1f} m/s", value).c_str());
  } else {
    climb = "0.0 m/s";
  }

  std::string meta;
  if (!parameter_copy.empty()) {
    const auto summary =
      XCThermAPI::Instance().GetCachedLayerSummary(parameter_copy);
    if (summary.latest_run_date.size() == 8 &&
        summary.latest_run_hour.size() == 2) {
      const std::string &d = summary.latest_run_date;
      meta += std::string(FmtBuffer<32>(" | run {}-{}-{} {}Z",
                                        d.substr(0, 4), d.substr(4, 2),
                                        d.substr(6, 2),
                                        summary.latest_run_hour).c_str());
    }
    if (summary.latest_downloaded_at > 0) {
      const std::time_t t = (std::time_t)summary.latest_downloaded_at;
      char tbuf[8];
      if (FormatLocalHourMinute(t, tbuf, sizeof(tbuf)))
        meta += std::string(FmtBuffer<16>(" | dl {}", tbuf).c_str());
    }
  }

  const auto line = FmtBuffer<256>("{} @ {} ({:02}Z){}",
                                   climb, label_copy, hour, meta);
  std::strncpy(buffer, line.c_str(), size - 1);
  buffer[size - 1] = '\0';
  return true;
}

const char *
XCThermGeoJSONOverlay::GetLabel() const noexcept
{
  return "XC Therm";
}

bool
XCThermGeoJSONOverlay::IsInside(GeoPoint p) const noexcept
{
  const std::lock_guard lock{mutex};
  /* Only claim the location if the forecast actually covers it — so a
     tap far outside the model domain doesn't surface a bogus XCTherm
     map item reading "0.0 m/s". */
  return !forecast.IsEmpty() && forecast.bounds.IsValid() &&
         forecast.bounds.IsInside(p);
}

Color
XCThermGeoJSONOverlay::WindToColor(double min_ms, double max_ms) noexcept
{
  /*
   * AROME-like HSL S=100% ramps on XCTherm bands:
   *
   *  ≤ -3.0     #00008F
   *  (-3,-2]    #0031B2
   *  (-2,-1]    #0076D6
   *  (-1,-0.5]  #00CAF5
   *  (-0.5,-0.2]#1AFFE8
   *  ±0.2       #E3E3A0
   *  (0.2,0.5]  #FFFF00
   *  (0.5,1]    #E3AA00
   *  (1,2]      #C76300
   *  (2,3]      #AB2B00
   *  (3,4]      #8F0000
   *  > +4.0     #8F008F
   */

  const double mid = (min_ms + max_ms) / 2.0;

  if (mid <= -3.0)  return COLOR_XCTHERM_BLUE;         // blue
  if (mid <= -2.0)  return COLOR_XCTHERM_BRIGHT_CYAN;  // bright cyan
  if (mid <= -1.0)  return COLOR_XCTHERM_SKY_BLUE;     // sky blue
  if (mid <= -0.5)  return COLOR_XCTHERM_LIGHT_BLUE;   // light blue
  if (mid <= -0.2)  return COLOR_XCTHERM_PALE_BLUE;    // pale blue
  if (mid <= +0.2)  return COLOR_XCTHERM_CREAM;        // cream/beige
  if (mid <= +0.5)  return COLOR_XCTHERM_YELLOW;       // yellow
  if (mid <= +1.0)  return COLOR_XCTHERM_GOLD;         // gold/amber
  if (mid <= +2.0)  return COLOR_XCTHERM_ORANGE;       // orange
  if (mid <= +3.0)  return COLOR_XCTHERM_RED_ORANGE;   // red-orange
  if (mid <= +4.0)  return COLOR_XCTHERM_RED;          // red
  return COLOR_XCTHERM_PURPLE;                         // purple
}

void
XCThermGeoJSONOverlay::Draw(Canvas &canvas,
                             const WindowProjection &projection) noexcept
{
  const std::lock_guard lock{mutex};

  if (forecast.IsEmpty())
    return;

#ifdef ENABLE_OPENGL
  /* Enable alpha blending for the entire overlay draw */
  const ScopeAlphaBlend alpha_blend;
#endif

  /* Integer screen verts (GeoToScreen). */
  std::vector<BulkPixelPoint> screen_points;
  screen_points.reserve(256);
  std::vector<unsigned> ring_sizes;
  ring_sizes.reserve(8);

  /* Same idea as TopographyFileRenderer: cull in geo space against
     GetScreenBounds(), which already covers a rotated viewport. */
  const GeoBounds &screen_bounds = projection.GetScreenBounds();
  const unsigned alpha =
    (opacity_percent * 255u + 50u) / 100u;

  for (const auto &band : forecast.bands) {
    const Color color = WindToColor(band.min_ms, band.max_ms);
    const Color fill_color = ColorWithAlpha(color, (uint8_t)alpha);

    Brush brush(fill_color);
    canvas.Select(brush);
    canvas.SelectNullPen();

    for (const auto &polygon : band.polygons) {
      if (polygon.empty() || polygon[0].size() < 3)
        continue;

      const auto &exterior = polygon[0];

      GeoBounds poly_bounds(exterior[0]);
      for (std::size_t i = 1; i < exterior.size(); ++i)
        poly_bounds.Extend(exterior[i]);

      if (!poly_bounds.Overlaps(screen_bounds))
        continue;

      screen_points.clear();
      ring_sizes.clear();
#ifdef ENABLE_OPENGL
      for (const auto &ring : polygon) {
        if (ring.size() < 3)
          continue;
        const unsigned before = (unsigned)screen_points.size();
        for (const auto &pt : ring) {
          const auto sp = projection.GeoToScreen(pt);
          screen_points.push_back(BulkPixelPoint{sp.x, sp.y});
        }
        ring_sizes.push_back((unsigned)screen_points.size() - before);
      }

      if (ring_sizes.empty())
        continue;

      canvas.DrawPolygon(screen_points.data(),
                         ring_sizes.data(),
                         (unsigned)ring_sizes.size());
#else
      /* Memory/GDI: fill exterior only (no hole support). */
      for (const auto &pt : exterior) {
        auto sp = projection.GeoToScreen(pt);
        screen_points.push_back(BulkPixelPoint{sp.x, sp.y});
      }

      canvas.DrawPolygon(screen_points.data(),
                         (unsigned)screen_points.size());
#endif
    }
  }
}
