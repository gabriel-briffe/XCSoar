// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeData.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"

#include <optional>
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

  /** Ground metres between adjacent cells east–west / north–south. */
  double cell_size_x_m = 0;
  double cell_size_y_m = 0;

  /** Fixed glide ratio (L/D) used by the field. */
  double glide_ratio = 1;

  float max_alt = 0;

  /** Seed (airport) cell. */
  int home_x = -1, home_y = -1;

  /** Seed cells with arrival altitudes (terrain + arrival height). */
  std::vector<GlideConeSeed> seeds;

  /**
   * Terrain plus ground clearance [m MSL], size width*height.  Used to
   * detect downhill-ground path segments (relative heights; clearance
   * cancels out).
   */
  std::vector<float> elevation;

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
    cell_size_m = cell_size_x_m = cell_size_y_m = 0;
    glide_ratio = 1;
    home_x = home_y = -1;
    seeds.clear();
    elevation.clear();
    contour_lines.clear();
  }

  /**
   * Build 100 m (or @p interval_m) altitude contours of the reachable
   * area (marching squares).  Always extracts the full raw segment set;
   * when @p polylines is true, stitches segments that share grid-edge
   * endpoints into continuous lines.  Fills #contour_lines.
   */
  void BuildContours(double interval_m = 100,
                     bool polylines = true) noexcept;

  /** Geographic position of the centre of grid cell (x,y). */
  [[gnu::pure]]
  GeoPoint CellToGeo(int x, int y) const noexcept;

  /** Grid cell containing a geographic position; false if outside grid. */
  bool GeoToCell(GeoPoint p, int &x, int &y) const noexcept;

  /** One cell on a relay-path trace. */
  struct TraceCell {
    int x, y;
  };

  /**
   * Trace the glide relay path from the given position back to the seed
   * by following origin pointers.  Returns an empty result if the start
   * cell is outside the grid or unreachable.
   */
  [[gnu::pure]]
  std::vector<TraceCell> Trace(GeoPoint from) const noexcept;

  /**
   * True when the segment from @p from to @p to is a downhill-ground
   * hop: the from-cell is ground and terrain falls toward the next
   * cell (gpu-MC isDownhillGroundSegment).
   */
  [[gnu::pure]]
  bool IsDownhillGroundSegment(int from_x, int from_y,
                               int to_x, int to_y) const noexcept;

  /**
   * Required arrival altitude [m MSL] at @p from for the InfoBox.
   *
   * Air cells use the stored required altitude.  On a ground cell the
   * stored value is terrain, so the relay path is walked back to the
   * first air cell and extra_alt = Euclidean cell distance / L/D is
   * added.  If the path is ground all the way to the seed, the result
   * is seed arrival altitude plus remaining distance / L/D.
   */
  [[gnu::pure]]
  std::optional<double> RequiredAltitude(GeoPoint from) const noexcept;
};
