// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeOptions.hpp"
#include "GlideConeField.hpp"
#include "Settings.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Brush.hpp"
#include "ui/canvas/Pen.hpp"
#include "ui/canvas/Color.hpp"
#include "ui/dim/BulkPoint.hpp"
#include "Look/Colors.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Scope.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

struct State {
  std::uint32_t request = 0;
  std::uint32_t done = 0;
  std::chrono::steady_clock::time_point last_at{};
  bool has_last = false;
  unsigned last_ms = 0;
  bool has_duration = false;

  unsigned width = 0, height = 0;
  GeoBounds bounds = GeoBounds::Invalid();
  double cell_x = 0, cell_y = 0;
  std::vector<std::uint8_t> mask;
  /** Descending arrival [m MSL] for option cells; unused elsewhere. */
  std::vector<float> arrival;
  /** Elected parent index per cell (-1 unused); used for pan path. */
  std::vector<int> origin;
  /** 1 where the cell is an upward-cone airport/landable seed. */
  std::vector<std::uint8_t> is_seed;
  /** Option seed cell with highest descending arrival; -1 if none. */
  int best_airport_index = -1;
  int start_index = -1;
  GeoPoint start_location = GeoPoint::Invalid();

  /** Pan-mode secondary downward from the probe option cell. */
  int pan_cell = -1;
  std::vector<int> pan_origin;
  std::vector<std::uint8_t> pan_mask;
  int pan_best_airport = -1;
  /** Settle-debounce: restarts while the probe cell keeps changing. */
  int pan_watch_cell = -1;
  std::chrono::steady_clock::time_point pan_since{};
};

/** Same settle window as glide-cone job debounce (heavy grid walk). */
static constexpr std::chrono::milliseconds PAN_DOWNWARD_DEBOUNCE{400};

State state;

[[gnu::pure]]
bool
GeoToCell(GeoPoint p, int &x, int &y) noexcept
{
  if (!p.IsValid() || !state.bounds.IsValid() ||
      state.width == 0 || state.height == 0)
    return false;

  const double width_native = state.bounds.GetWidth().Native();
  const double height_native = state.bounds.GetHeight().Native();
  if (width_native <= 0 || height_native <= 0)
    return false;

  const double fx =
    (p.longitude - state.bounds.GetWest()).Native() / width_native;
  const double fy =
    (state.bounds.GetNorth() - p.latitude).Native() / height_native;
  if (fx < 0 || fx >= 1 || fy < 0 || fy >= 1)
    return false;

  x = std::clamp(int(fx * state.width), 0, int(state.width) - 1);
  y = std::clamp(int(fy * state.height), 0, int(state.height) - 1);
  return true;
}

[[gnu::pure]]
GeoPoint
CellToGeo(int x, int y) noexcept
{
  if (!state.bounds.IsValid() || state.width == 0 || state.height == 0)
    return GeoPoint::Invalid();

  const double fx = (x + 0.5) / double(state.width);
  const double fy = (y + 0.5) / double(state.height);
  return GeoPoint(state.bounds.GetWest() + state.bounds.GetWidth() * fx,
                  state.bounds.GetNorth() - state.bounds.GetHeight() * fy);
}

struct Heap {
  std::vector<int> index;
  std::vector<float> arrival;

  void Push(int i, float a) noexcept {
    index.push_back(i);
    arrival.push_back(a);
    SiftUp(index.size() - 1);
  }

  bool Empty() const noexcept { return index.empty(); }

  std::pair<int, float> Pop() noexcept {
    const int i = index[0];
    const float a = arrival[0];
    index[0] = index.back();
    arrival[0] = arrival.back();
    index.pop_back();
    arrival.pop_back();
    SiftDown(0);
    return {i, a};
  }

  void SiftUp(std::size_t i) noexcept {
    while (i > 0) {
      const std::size_t parent = (i - 1) / 2;
      if (arrival[parent] >= arrival[i])
        break;
      Swap(parent, i);
      i = parent;
    }
  }

  void SiftDown(std::size_t i) noexcept {
    const std::size_t n = index.size();
    while (true) {
      std::size_t best = i;
      const std::size_t left = i * 2 + 1;
      const std::size_t right = left + 1;
      if (left < n && arrival[left] > arrival[best])
        best = left;
      if (right < n && arrival[right] > arrival[best])
        best = right;
      if (best == i)
        break;
      Swap(i, best);
      i = best;
    }
  }

  void Swap(std::size_t a, std::size_t b) noexcept {
    std::swap(index[a], index[b]);
    std::swap(arrival[a], arrival[b]);
  }
};

/**
 * Downward optional area: the upward glide-cone altitudes are the floor
 * (not terrain).  FLAG-like "GC" cells mean the wavefront already hit
 * that cone — they hard-stop LOS and neighbour expansion (gpu-MC
 * downward-shader semantics).
 */
constexpr std::uint8_t GC = 1;

[[gnu::pure]]
bool
HasConeFloor(const GlideConeField &field, std::size_t i) noexcept
{
  return field.result.altitudes[i] < field.max_alt;
}

/**
 * Bresenham LOS from (@p x0,@p y0) to origin (@p ox,@p oy).  Blocked by
 * GC cells and by intermediates whose descent altitude from the origin
 * would fall below the upward glide-cone floor.
 */
bool
InView(const std::vector<std::uint8_t> &flags,
       const std::vector<float> &best,
       const GlideConeField &field,
       unsigned width, unsigned height,
       int x0, int y0, int ox, int oy,
       double cell_x, double cell_y, double ratio) noexcept
{
  const std::size_t origin_i = std::size_t(oy) * width + ox;
  const float origin_alt = best[origin_i];
  int dx = std::abs(ox - x0);
  int dy = std::abs(oy - y0);
  int sx = x0 < ox ? 1 : -1;
  int sy = y0 < oy ? 1 : -1;
  int err = dx - dy;
  int x = x0, y = y0;
  while (x != ox || y != oy) {
    const int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x += sx; }
    if (e2 < dx) { err += dx; y += sy; }
    if (x == ox && y == oy)
      break;
    if (x < 0 || y < 0 || x >= int(width) || y >= int(height))
      return false;
    const std::size_t i = std::size_t(y) * width + x;
    if (flags[i] & GC)
      return false;
    if (!HasConeFloor(field, i))
      return false;
    const float alt = origin_alt -
      float(std::hypot((x - ox) * cell_x, (y - oy) * cell_y) / ratio);
    if (alt < field.result.altitudes[i])
      return false;
  }
  return true;
}

/**
 * Downward options wavefront from (@p gi,@p gj) at @p start_alt.
 * Fills @p best / @p origin / @p flags (size width*height).
 * @return false if the start cell cannot begin an optional area.
 */
bool
PropagateDownward(const GlideConeField &field, int gi, int gj,
                  float start_alt,
                  std::vector<float> &best,
                  std::vector<int> &origin,
                  std::vector<std::uint8_t> &flags) noexcept
{
  const unsigned width = field.result.width;
  const unsigned height = field.result.height;
  const std::size_t n = std::size_t(width) * height;
  if (gi < 0 || gj < 0 || gi >= int(width) || gj >= int(height))
    return false;

  best.assign(n, -1.f);
  origin.assign(n, -1);
  flags.assign(n, 0);

  const std::size_t start = std::size_t(gj) * width + gi;
  const float terrain_floor =
    field.elevation.size() == n ? field.elevation[start] : start_alt;
  const float cone = field.result.altitudes[start];
  if (start_alt < terrain_floor || !HasConeFloor(field, start) ||
      start_alt < cone)
    return false;

  best[start] = start_alt;
  origin[start] = int(start);
  Heap heap;
  heap.Push(int(start), start_alt);

  const double cell_x = field.cell_size_x_m > 0 ? field.cell_size_x_m
                                               : field.cell_size_m;
  const double cell_y = field.cell_size_y_m > 0 ? field.cell_size_y_m
                                               : field.cell_size_m;
  const double ratio = field.glide_ratio > 0 ? field.glide_ratio : 1;

  while (!heap.Empty()) {
    const auto [index, arrival] = heap.Pop();
    const std::size_t ui = std::size_t(index);
    if (flags[ui] & GC)
      continue;
    if (arrival < best[ui] - 0.05f)
      continue;
    const int x = index % int(width);
    const int y = index / int(width);
    const int from_origin = origin[ui];
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0)
          continue;
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height))
          continue;
        const std::size_t nidx = std::size_t(ny) * width + nx;
        if (flags[nidx] & GC)
          continue;

        int elected = index;
        if (from_origin >= 0 &&
            InView(flags, best, field, width, height, nx, ny,
                   from_origin % int(width),
                   from_origin / int(width),
                   cell_x, cell_y, ratio))
          elected = from_origin;

        if (flags[std::size_t(elected)] & GC)
          continue;

        const int ox = elected % int(width);
        const int oy = elected / int(width);
        const double dist = std::hypot((nx - ox) * cell_x, (ny - oy) * cell_y);
        const float next = best[std::size_t(elected)] - float(dist / ratio);

        if (HasConeFloor(field, nidx) &&
            next < field.result.altitudes[nidx]) {
          if (best[nidx] >= field.result.altitudes[nidx])
            continue;
          if (!(flags[nidx] & GC)) {
            best[nidx] = field.result.altitudes[nidx];
            origin[nidx] = elected;
            flags[nidx] = GC;
          }
          continue;
        }

        if (next <= best[nidx])
          continue;
        best[nidx] = next;
        origin[nidx] = elected;
        heap.Push(int(nidx), next);
      }
    }
  }
  return true;
}

void
BuildMask(const GlideConeField &field,
          const std::vector<float> &best,
          const std::vector<std::uint8_t> &flags,
          std::vector<std::uint8_t> &mask,
          std::vector<float> &arrival) noexcept
{
  const std::size_t n = best.size();
  mask.assign(n, 0);
  arrival.assign(n, -1.f);
  for (std::size_t i = 0; i < n; ++i)
    if (!(flags[i] & GC) && best[i] >= 0 && HasConeFloor(field, i) &&
        best[i] >= field.result.altitudes[i]) {
      mask[i] = 1;
      arrival[i] = best[i];
    }
}

[[gnu::pure]]
int
BestAirportIndex(const std::vector<std::uint8_t> &mask,
                 const std::vector<float> &arrival,
                 const std::vector<std::uint8_t> &is_seed) noexcept
{
  int best_i = -1;
  float best_a = -1.f;
  for (std::size_t i = 0; i < mask.size(); ++i) {
    if (!is_seed[i] || !mask[i] || arrival[i] < 0)
      continue;
    if (arrival[i] > best_a) {
      best_a = arrival[i];
      best_i = int(i);
    }
  }
  return best_i;
}

} // namespace

void
GlideConeOptions::RequestOnce() noexcept
{
  ++state.request;
}

void
GlideConeOptions::Clear() noexcept
{
  state.mask.clear();
  state.arrival.clear();
  state.origin.clear();
  state.is_seed.clear();
  state.best_airport_index = -1;
  state.start_index = -1;
  state.start_location = GeoPoint::Invalid();
  state.width = state.height = 0;
  state.bounds = GeoBounds::Invalid();
  state.pan_cell = -1;
  state.pan_origin.clear();
  state.pan_mask.clear();
  state.pan_best_airport = -1;
  state.pan_watch_cell = -1;
  state.done = state.request;
}

std::optional<double>
GlideConeOptions::QueryArrivalAltitude(GeoPoint location) noexcept
{
  if (!location.IsValid() || state.mask.empty() ||
      state.arrival.size() != state.mask.size())
    return std::nullopt;

  int x, y;
  if (!GeoToCell(location, x, y))
    return std::nullopt;

  const std::size_t i = std::size_t(y) * state.width + x;
  if (!state.mask[i] || state.arrival[i] < 0)
    return std::nullopt;
  return double(state.arrival[i]);
}

std::optional<unsigned>
GlideConeOptions::LastComputeMs() noexcept
{
  if (!state.has_duration)
    return std::nullopt;
  return state.last_ms;
}

void
GlideConeOptions::Update(const GlideConeField &field, double start_alt,
                         GeoPoint start_location, int gi, int gj,
                         const GlideConeSettings &settings) noexcept
{
  if (settings.options_mode == GlideConeSettings::OptionsMode::OFF) {
    Clear();
    return;
  }
  if (!field.IsValid() || !std::isfinite(start_alt))
    return;

  const auto now = std::chrono::steady_clock::now();
  const bool once = settings.options_mode == GlideConeSettings::OptionsMode::ONCE;
  if (once) {
    if (state.request == state.done && !state.mask.empty())
      return;
    if (state.request == 0)
      return;
  } else {
    const auto interval = std::chrono::seconds(
      std::clamp(settings.options_routine_s, 1u, 60u));
    if (state.has_last && now - state.last_at < interval && state.request == state.done)
      return;
  }

  const unsigned width = field.result.width;
  const unsigned height = field.result.height;
  const std::size_t n = std::size_t(width) * height;
  if (gi < 0 || gj < 0 || gi >= int(width) || gj >= int(height))
    return;

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<float> best;
  std::vector<int> origin;
  std::vector<std::uint8_t> flags;
  if (!PropagateDownward(field, gi, gj, float(start_alt),
                         best, origin, flags)) {
    /* Already at/below the upward cone (or terrain): no optional area. */
    Clear();
    state.has_last = true;
    state.last_at = std::chrono::steady_clock::now();
    return;
  }

  BuildMask(field, best, flags, state.mask, state.arrival);
  state.origin = std::move(origin);
  state.is_seed.assign(n, 0);
  for (const auto &s : field.seeds) {
    if (s.x < 0 || s.y < 0 || s.x >= int(width) || s.y >= int(height))
      continue;
    state.is_seed[std::size_t(s.y) * width + s.x] = 1;
  }
  state.best_airport_index =
    BestAirportIndex(state.mask, state.arrival, state.is_seed);
  state.start_index = int(std::size_t(gj) * width + gi);
  state.width = width;
  state.height = height;
  state.bounds = field.bounds;
  state.cell_x = field.cell_size_x_m > 0 ? field.cell_size_x_m
                                         : field.cell_size_m;
  state.cell_y = field.cell_size_y_m > 0 ? field.cell_size_y_m
                                         : field.cell_size_m;
  state.start_location = start_location.IsValid()
    ? start_location
    : field.CellToGeo(gi, gj);
  state.pan_cell = -1;
  state.pan_origin.clear();
  state.pan_mask.clear();
  state.pan_best_airport = -1;
  state.pan_watch_cell = -1;
  state.done = state.request;
  state.has_last = true;
  state.last_at = std::chrono::steady_clock::now();
  state.last_ms = unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(
    state.last_at - t0).count());
  state.has_duration = true;
}

void
GlideConeOptions::Draw(Canvas &canvas, const WindowProjection &projection,
                       const GlideConeSettings &settings) noexcept
{
  if (settings.options_mode == GlideConeSettings::OptionsMode::OFF ||
      state.mask.empty() || !state.bounds.IsValid())
    return;

  unsigned opacity = std::clamp(settings.options_opacity, 20u, 100u);
  const Color color(COLOR_GLIDE_CONE.Red(), COLOR_GLIDE_CONE.Green(),
                    COLOR_GLIDE_CONE.Blue(),
                    std::uint8_t(opacity * 255 / 100));
  canvas.SelectNullPen();
  canvas.Select(Brush(color));

#ifdef ENABLE_OPENGL
  const ScopeAlphaBlend alpha_blend;
#endif

  const PixelRect screen = projection.GetScreenRect();
  for (unsigned y = 0; y < state.height; ++y) {
    for (unsigned x = 0; x < state.width; ++x) {
      if (!state.mask[std::size_t(y) * state.width + x])
        continue;
      const GeoPoint center = state.bounds.GetCenter();
      (void)center;
      const double west = state.bounds.GetWest().Degrees() +
        (double(x) / state.width) * state.bounds.GetWidth().Degrees();
      const double east = state.bounds.GetWest().Degrees() +
        (double(x + 1) / state.width) * state.bounds.GetWidth().Degrees();
      const double north = state.bounds.GetNorth().Degrees() -
        (double(y) / state.height) * state.bounds.GetHeight().Degrees();
      const double south = state.bounds.GetNorth().Degrees() -
        (double(y + 1) / state.height) * state.bounds.GetHeight().Degrees();
      const GeoPoint corners[4] = {
        GeoPoint(Angle::Degrees(west), Angle::Degrees(north)),
        GeoPoint(Angle::Degrees(east), Angle::Degrees(north)),
        GeoPoint(Angle::Degrees(east), Angle::Degrees(south)),
        GeoPoint(Angle::Degrees(west), Angle::Degrees(south)),
      };
      BulkPixelPoint pts[4];
      bool visible = false;
      for (unsigned i = 0; i < 4; ++i) {
        pts[i] = projection.GeoToScreen(corners[i]);
        if (pts[i].x >= screen.left && pts[i].x <= screen.right &&
            pts[i].y >= screen.top && pts[i].y <= screen.bottom)
          visible = true;
      }
      if (visible)
        canvas.DrawPolygon(pts, 4);
    }
  }
}

/**
 * Polyline from @p start_location (@p start_index) to @p end_location
 * (@p end_index) following @p origin.  @p reachable must be true for
 * the end cell.
 */
bool
BuildOriginPath(const std::vector<int> &origin,
                const std::vector<std::uint8_t> &reachable,
                int start_index, GeoPoint start_location,
                int end_index, GeoPoint end_location,
                const WindowProjection &projection,
                std::vector<BulkPixelPoint> &pts) noexcept
{
  pts.clear();
  if (end_index < 0 || start_index < 0 || !end_location.IsValid() ||
      !start_location.IsValid() || origin.empty() ||
      origin.size() != reachable.size() ||
      !reachable[std::size_t(end_index)])
    return false;

  std::vector<int> cells;
  cells.reserve(64);
  int cur = end_index;
  const unsigned max_steps = (state.width + state.height) * 2;
  for (unsigned step = 0; step < max_steps; ++step) {
    cells.push_back(cur);
    if (cur == start_index)
      break;
    if (cur < 0 || std::size_t(cur) >= origin.size())
      return false;
    const int parent = origin[std::size_t(cur)];
    if (parent < 0 || parent == cur)
      return false;
    cur = parent;
  }
  if (cells.empty() || cells.back() != start_index)
    return false;

  std::reverse(cells.begin(), cells.end());

  pts.reserve(cells.size());
  pts.push_back(projection.GeoToScreen(start_location));
  for (std::size_t i = 1; i + 1 < cells.size(); ++i) {
    const int cx = cells[i] % int(state.width);
    const int cy = cells[i] / int(state.width);
    const GeoPoint geo = CellToGeo(cx, cy);
    if (!geo.IsValid())
      return false;
    pts.push_back(projection.GeoToScreen(geo));
  }
  pts.push_back(projection.GeoToScreen(end_location));
  return pts.size() >= 2;
}

void
StrokePath(Canvas &canvas, const std::vector<BulkPixelPoint> &pts,
           const Pen &border, const Pen &stroke) noexcept
{
  canvas.Select(border);
  canvas.DrawPolyline(pts.data(), unsigned(pts.size()));
  canvas.Select(stroke);
  canvas.DrawPolyline(pts.data(), unsigned(pts.size()));
}

/**
 * Ensure a secondary downward from @p pan_cell at its option altitude.
 * Settle-debounced: the probe cell must stay put for
 * #PAN_DOWNWARD_DEBOUNCE before the heavy walk runs.
 */
bool
EnsurePanAirportPath(const GlideConeField &field, int pan_cell) noexcept
{
  if (pan_cell < 0 || std::size_t(pan_cell) >= state.mask.size() ||
      !state.mask[std::size_t(pan_cell)])
    return false;

  if (state.pan_cell == pan_cell && !state.pan_origin.empty())
    return state.pan_best_airport >= 0;

  const auto now = std::chrono::steady_clock::now();
  if (state.pan_watch_cell != pan_cell) {
    /* Probe moved: restart settle timer and drop a stale green path. */
    state.pan_watch_cell = pan_cell;
    state.pan_since = now;
    state.pan_cell = -1;
    state.pan_origin.clear();
    state.pan_mask.clear();
    state.pan_best_airport = -1;
    return false;
  }

  if (now - state.pan_since < PAN_DOWNWARD_DEBOUNCE)
    return false;

  state.pan_cell = pan_cell;
  state.pan_origin.clear();
  state.pan_mask.clear();
  state.pan_best_airport = -1;

  const int gx = pan_cell % int(state.width);
  const int gy = pan_cell / int(state.width);
  const float start_alt = state.arrival[std::size_t(pan_cell)];
  std::vector<float> best;
  std::vector<std::uint8_t> flags;
  std::vector<float> arrival;
  if (!PropagateDownward(field, gx, gy, start_alt,
                         best, state.pan_origin, flags))
    return false;

  BuildMask(field, best, flags, state.pan_mask, arrival);
  state.pan_best_airport =
    BestAirportIndex(state.pan_mask, arrival, state.is_seed);
  return state.pan_best_airport >= 0;
}

bool
GlideConeOptions::DrawBestAirportPath(Canvas &canvas,
                                      const WindowProjection &projection,
                                      const MapLook &look) noexcept
{
  if (state.best_airport_index < 0)
    return false;

  const int end = state.best_airport_index;
  const int x = end % int(state.width);
  const int y = end / int(state.width);
  const GeoPoint end_geo = CellToGeo(x, y);
  std::vector<BulkPixelPoint> pts;
  if (!BuildOriginPath(state.origin, state.mask,
                       state.start_index, state.start_location,
                       end, end_geo, projection, pts))
    return false;

  StrokePath(canvas, pts, look.glide_cone_border_pen,
             look.glide_cone_options_pen);
  return true;
}

bool
GlideConeOptions::DrawPanPaths(Canvas &canvas,
                               const WindowProjection &projection,
                               GeoPoint pan_probe,
                               const GlideConeField &field,
                               const MapLook &look) noexcept
{
  if (!pan_probe.IsValid() || state.mask.empty() ||
      state.origin.size() != state.mask.size() ||
      state.start_index < 0 || !state.start_location.IsValid())
    return false;

  int x, y;
  if (!GeoToCell(pan_probe, x, y))
    return false;

  const int pan_cell = y * int(state.width) + x;
  if (!state.mask[std::size_t(pan_cell)])
    return false;

  /* Pink: aircraft → option cell under the probe. */
  std::vector<BulkPixelPoint> pink;
  if (!BuildOriginPath(state.origin, state.mask,
                       state.start_index, state.start_location,
                       pan_cell, pan_probe, projection, pink))
    return false;
  StrokePath(canvas, pink, look.glide_cone_border_pen, look.glide_cone_pen);

  /* Green: fresh downward from that cell to the best airport. */
  if (!EnsurePanAirportPath(field, pan_cell))
    return true;

  const int airport = state.pan_best_airport;
  const int ax = airport % int(state.width);
  const int ay = airport / int(state.width);
  const GeoPoint airport_geo = CellToGeo(ax, ay);
  std::vector<BulkPixelPoint> green;
  if (BuildOriginPath(state.pan_origin, state.pan_mask,
                      pan_cell, pan_probe,
                      airport, airport_geo, projection, green))
    StrokePath(canvas, green, look.glide_cone_border_pen,
               look.glide_cone_options_pen);
  return true;
}
