// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeField.hpp"

#include <algorithm>

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
