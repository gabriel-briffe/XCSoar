// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeField.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

GeoPoint
GlideConeField::CellToGeo(int x, int y) const noexcept
{
  const double fx = (x + 0.5) / double(result.width);
  const double fy = (y + 0.5) / double(result.height);
  const Angle lng = bounds.GetWest() + bounds.GetWidth() * fx;
  const Angle lat = bounds.GetNorth() - bounds.GetHeight() * fy;
  return GeoPoint(lng, lat);
}

bool
GlideConeField::GeoToCell(GeoPoint p, int &x, int &y) const noexcept
{
  const double width_native = bounds.GetWidth().Native();
  const double height_native = bounds.GetHeight().Native();
  if (width_native <= 0 || height_native <= 0)
    return false;

  const double fx = (p.longitude - bounds.GetWest()).Native() / width_native;
  const double fy = (bounds.GetNorth() - p.latitude).Native() / height_native;
  if (fx < 0 || fx >= 1 || fy < 0 || fy >= 1)
    return false;

  x = std::clamp(int(fx * result.width), 0, int(result.width) - 1);
  y = std::clamp(int(fy * result.height), 0, int(result.height) - 1);
  return true;
}

std::vector<GeoPoint>
GlideConeField::Trace(GeoPoint from) const noexcept
{
  std::vector<GeoPoint> path;
  if (!IsValid())
    return path;

  int x, y;
  if (!GeoToCell(from, x, y))
    return path;

  const unsigned width = result.width;
  const unsigned height = result.height;

  /* bail out if the start cell is unreachable */
  const std::size_t start_index = std::size_t(y) * width + x;
  if (result.altitudes[start_index] >= max_alt)
    return path;

  std::vector<bool> visited(std::size_t(width) * height, false);
  const unsigned max_steps = (width + height) * 2;

  for (unsigned step = 0; step < max_steps; ++step) {
    const std::size_t index = std::size_t(y) * width + x;
    if (visited[index])
      break;
    visited[index] = true;

    path.push_back(CellToGeo(x, y));

    if (x == home_x && y == home_y)
      break;

    const std::int32_t nx = result.origin_x[index];
    const std::int32_t ny = result.origin_y[index];
    if (nx < 0 || ny < 0 || (nx == x && ny == y))
      break;

    x = nx;
    y = ny;
  }

  if (path.size() < 2)
    path.clear();

  return path;
}

/* marching squares tables (see gpu-MC contours.js) */
namespace {
constexpr int CORNER_OFFSETS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
constexpr int EDGE_VERTICES[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};

struct EdgePair { int a, b; };
struct CaseSegments {
  unsigned count;
  EdgePair seg[2];
};

constexpr CaseSegments MS_SEGMENTS[16] = {
  {0, {}},
  {1, {{3, 0}}},
  {1, {{0, 1}}},
  {1, {{3, 1}}},
  {1, {{1, 2}}},
  {2, {{3, 0}, {1, 2}}},
  {1, {{0, 2}}},
  {1, {{3, 2}}},
  {1, {{2, 3}}},
  {1, {{2, 0}}},
  {2, {{0, 1}, {2, 3}}},
  {1, {{2, 1}}},
  {1, {{1, 3}}},
  {1, {{1, 0}}},
  {1, {{0, 3}}},
  {0, {}},
};

[[gnu::pure]]
GeoPoint Lerp(GeoPoint a, GeoPoint b, double t) noexcept
{
  return GeoPoint(a.longitude + (b.longitude - a.longitude) * t,
                  a.latitude + (b.latitude - a.latitude) * t);
}
} // anonymous namespace

void
GlideConeField::BuildContours(double interval_m) noexcept
{
  contour_segments.clear();
  if (!IsValid() || interval_m <= 0)
    return;

  const unsigned w = result.width;
  const unsigned h = result.height;
  const float max_alt_f = max_alt;
  const bool have_ground = !result.ground.empty();

  const auto valid_alt = [&](unsigned i, unsigned j) -> std::optional<float> {
    const std::size_t idx = std::size_t(j) * w + i;
    if (have_ground && result.ground[idx])
      return std::nullopt;
    if (result.origin_x[idx] < 0)
      return std::nullopt;
    const float a = result.altitudes[idx];
    if (!(a < max_alt_f))
      return std::nullopt;
    return a;
  };

  float max_reachable = 0;
  for (std::size_t i = 0; i < result.altitudes.size(); ++i) {
    if ((!have_ground || !result.ground[i]) && result.origin_x[i] >= 0) {
      const float a = result.altitudes[i];
      if (a < max_alt_f && a > max_reachable)
        max_reachable = a;
    }
  }

  const int max_level = int(std::floor(max_reachable / interval_m) * interval_m);

  for (int level = int(interval_m); level <= max_level;
       level += int(interval_m)) {
    const float flevel = float(level);

    for (unsigned j = 0; j + 1 < h; ++j) {
      for (unsigned i = 0; i + 1 < w; ++i) {
        std::array<float, 4> values;
        bool ok = true;
        for (unsigned c = 0; c < 4; ++c) {
          const auto v = valid_alt(i + CORNER_OFFSETS[c][0],
                                   j + CORNER_OFFSETS[c][1]);
          if (!v) { ok = false; break; }
          values[c] = *v;
        }
        if (!ok)
          continue;

        unsigned case_index = 0;
        for (unsigned c = 0; c < 4; ++c)
          if (values[c] >= flevel)
            case_index |= 1u << c;
        if (case_index == 0 || case_index == 15)
          continue;

        std::array<GeoPoint, 4> corners;
        for (unsigned c = 0; c < 4; ++c)
          corners[c] = CellToGeo(i + CORNER_OFFSETS[c][0],
                                 j + CORNER_OFFSETS[c][1]);

        const auto edge_point = [&](int edge) -> std::optional<GeoPoint> {
          const int a = EDGE_VERTICES[edge][0];
          const int b = EDGE_VERTICES[edge][1];
          const float z1 = values[a], z2 = values[b];
          if (z1 == z2)
            return std::nullopt;
          const double t = (flevel - z1) / (z2 - z1);
          if (t < 0 || t > 1)
            return std::nullopt;
          return Lerp(corners[a], corners[b], t);
        };

        const CaseSegments &cs = MS_SEGMENTS[case_index];
        for (unsigned s = 0; s < cs.count; ++s) {
          const auto p0 = edge_point(cs.seg[s].a);
          const auto p1 = edge_point(cs.seg[s].b);
          if (p0 && p1) {
            contour_segments.push_back(*p0);
            contour_segments.push_back(*p1);
          }
        }
      }
    }
  }
}
