// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeData.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"

#include <utility>
#include <vector>

/**
 * A computed glide cone field plus the georeferencing needed to map grid
 * cells to geographic coordinates and to trace the relay path.
 *
 * This is a pure-data helper (no GPU/GL dependencies) so it can be used on
 * any target and by the map draw thread.
 */
struct GlideConeField {
  GlideConeResult result;

  GeoBounds bounds = GeoBounds::Invalid();

  double cell_size_m = 0;

  float max_alt = 0;

  /** Seed (airport) cell. */
  int home_x = -1, home_y = -1;

  /** A contour polyline at a given altitude level. */
  struct ContourLine {
    std::vector<GeoPoint> points;
    int level;
  };

  /** Altitude contour polylines, built on demand by BuildContours(). */
  std::vector<ContourLine> contour_lines;

  [[gnu::pure]]
  bool IsValid() const noexcept {
    return result.IsValid() && bounds.IsValid();
  }

  void Clear() noexcept {
    result.Clear();
    bounds.SetInvalid();
    home_x = home_y = -1;
    contour_lines.clear();
  }

  /**
   * Build 100 m (or @p interval_m) altitude contour polylines of the
   * reachable area (marching squares, edge-walked into continuous
   * lines).  Fills #contour_lines.
   */
  void BuildContours(double interval_m = 100) noexcept;

  /** Geographic position of the centre of grid cell (x,y). */
  [[gnu::pure]]
  GeoPoint CellToGeo(int x, int y) const noexcept;

  /** Grid cell containing a geographic position; false if outside grid. */
  bool GeoToCell(GeoPoint p, int &x, int &y) const noexcept;

  /**
   * Trace the glide relay path from the given position back to the seed
   * by following origin pointers.  Returns an empty result if the start
   * cell is outside the grid or unreachable.
   */
  [[gnu::pure]]
  std::vector<GeoPoint> Trace(GeoPoint from) const noexcept;
};
