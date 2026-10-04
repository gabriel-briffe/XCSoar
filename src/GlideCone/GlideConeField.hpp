// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeData.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"

#include <cstdint>
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

  /**
   * Reused cycle-detection stamps for Trace /
   * RidgeSoaringProofGlideConeAltitude.  Epoch bump avoids clearing
   * the whole grid each call.
   */
  mutable std::vector<std::uint32_t> visit_stamp;
  mutable std::uint32_t visit_epoch = 0;

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
    visit_stamp.clear();
    visit_epoch = 0;
  }

  /**
   * Build 100 m (or @p interval_m) altitude contours of the reachable
   * area (marching squares), then stitch segments that share grid-edge
   * endpoints into continuous polylines.  Fills #contour_lines.
   */
  void BuildContours(double interval_m = 100) noexcept;

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
   *
   * Path geometry for drawing / distance uses @p from as the first
   * vertex, then cell centres from the second cell onward.
   */
  std::vector<TraceCell> Trace(GeoPoint from) const noexcept;

  /**
   * True when the segment from @p from to @p to is a downhill-ground
   * hop: the from-cell is ground and terrain falls toward the next
   * cell (gpu-MC isDownhillGroundSegment).
   */
  [[gnu::pure]]
  bool IsDownhillGroundSegment(int from_x, int from_y,
                               int to_x, int to_y) const noexcept;

  /** True when grid cell (@p x,@p y) is a ground (terrain-hit) cell. */
  [[gnu::pure]]
  bool IsGroundAt(int x, int y) const noexcept;

  /**
   * Stored field altitude [m MSL] at @p from: required height on air
   * cells, terrain on ground cells.  Empty if outside / unreachable.
   * Used for pan GlideCone label and critical-path discs.
   */
  std::optional<double> StoredAltitude(GeoPoint from) const noexcept;

  /**
   * InfoBox GlideCone altitude [m MSL] at @p from.
   *
   * Air cells: stored field altitude.  Ground cells store terrain, so
   * this walks origin toward the seed to the first air cell and adds
   * distance / L/D (or seed arrival if the path is ground all the way).
   * That avoids a large negative margin on the ridge while the stored
   * terrain height would show a huge positive margin once clear of
   * ground clearance — a ridge-soaring display trick, not the pan
   * label or critical-path red discs.
   */
  std::optional<double>
  RidgeSoaringProofGlideConeAltitude(GeoPoint from) const noexcept;

  /**
   * Total Euclidean ground distance [m] along the relay path from
   * @p from to the seed: first hop from @p from to the next cell
   * centre, then cell-centre hops.  Empty when no path exists.
   */
  std::optional<double> PathDistance(GeoPoint from) const noexcept;

  /**
   * Waypoint id of the landable at the end of the relay path from
   * @p from (0 if unknown / no path).
   */
  [[gnu::pure]]
  unsigned PathDestinationWaypointId(GeoPoint from) const noexcept;
};
