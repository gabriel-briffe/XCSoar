// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeOptions.hpp"
#include "GlideConeField.hpp"
#include "Settings.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Brush.hpp"
#include "ui/canvas/Pen.hpp"
#include "ui/canvas/Color.hpp"
#include "Look/Colors.hpp"
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
};

State state;

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

bool
InView(const std::vector<std::uint8_t> &flags, unsigned width,
       unsigned height, int x0, int y0, int x1, int y1) noexcept
{
  int dx = std::abs(x1 - x0);
  int dy = std::abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  int x = x0, y = y0;
  while (x != x1 || y != y1) {
    const int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x += sx; }
    if (e2 < dx) { err += dx; y += sy; }
    if (x == x1 && y == y1)
      break;
    if (x < 0 || y < 0 || x >= int(width) || y >= int(height))
      return false;
    if (flags[std::size_t(y) * width + x] & GC)
      return false;
  }
  return true;
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
  state.width = state.height = 0;
  state.bounds = GeoBounds::Invalid();
  state.done = state.request;
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
                         int gi, int gj,
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
  std::vector<float> best(n, -1.f);
  std::vector<int> origin(n, -1);
  std::vector<std::uint8_t> flags(n, 0);
  const std::size_t start = std::size_t(gj) * width + gi;
  const float start_f = float(start_alt);
  const float terrain_floor =
    field.elevation.size() == n ? field.elevation[start] : start_f;
  const float cone = field.result.altitudes[start];
  if (start_f < terrain_floor || !HasConeFloor(field, start) ||
      start_f < cone) {
    /* Already at/below the upward cone (or terrain): no optional area.
       Drop the previous mask so the last patch does not linger. */
    Clear();
    state.has_last = true;
    state.last_at = std::chrono::steady_clock::now();
    return;
  }

  best[start] = start_f;
  origin[start] = int(start);
  Heap heap;
  heap.Push(int(start), start_f);

  const double cell_x = field.cell_size_x_m > 0 ? field.cell_size_x_m : field.cell_size_m;
  const double cell_y = field.cell_size_y_m > 0 ? field.cell_size_y_m : field.cell_size_m;
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
        /* GC neighbours are a hard stop. */
        if (flags[nidx] & GC)
          continue;

        int elected = index;
        if (from_origin >= 0 &&
            InView(flags, width, height, nx, ny,
                   from_origin % int(width),
                   from_origin / int(width)))
          elected = from_origin;

        if (flags[std::size_t(elected)] & GC)
          continue;

        const int ox = elected % int(width);
        const int oy = elected / int(width);
        const double dist = std::hypot((nx - ox) * cell_x, (ny - oy) * cell_y);
        const float next = best[std::size_t(elected)] - float(dist / ratio);

        /* Upward glide cone is the floor (not DEM elevation). */
        if (HasConeFloor(field, nidx) &&
            next < field.result.altitudes[nidx]) {
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

  /* Green where descending arrival is at or above the cone. */
  state.mask.assign(n, 0);
  for (std::size_t i = 0; i < n; ++i)
    if (!(flags[i] & GC) && best[i] >= 0 && HasConeFloor(field, i) &&
        best[i] >= field.result.altitudes[i])
      state.mask[i] = 1;
  state.width = width;
  state.height = height;
  state.bounds = field.bounds;
  state.cell_x = cell_x;
  state.cell_y = cell_y;
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
