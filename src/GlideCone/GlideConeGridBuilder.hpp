// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeData.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"
#include "Renderer/WaypointRendererSettings.hpp"

#include <cstdint>
#include <vector>

class RasterTerrain;
class Waypoints;

/** Hard cap on GPU grid cells per side. */
static constexpr unsigned GLIDE_CONE_MAX_DIM = 1024;

/**
 * CPU-side inputs for building a glide-cone DEM grid.  No GL.
 */
struct GlideConeGridRequest {
  std::uint64_t generation = 0;

  GeoPoint center = GeoPoint::Invalid();
  double radius_m = 0;

  double glide_ratio = 25;
  double max_altitude = 3000;
  double cell_size = 400;
  unsigned iteration_cap = 2000;

  double clearance = 0;
  double arrival = 0;

  bool combined = false;

  /** Single-mode seed(s); combined mode gathers landables itself. */
  std::vector<GeoPoint> seeds;

  WaypointRendererSettings waypoint_settings{};
};

/**
 * A DEM grid plus the georeferencing needed to install a GPU result
 * into a #GlideConeField.
 */
struct GlideConePreparedGrid {
  std::uint64_t generation = 0;
  GeoPoint center = GeoPoint::Invalid();
  GeoBounds bounds = GeoBounds::Invalid();
  GlideConeGrid grid;
};

/**
 * Max-pool the DEM and place seeds.  Uses #RasterTerrain::Lease.
 * Combined mode visits @p waypoints (may be nullptr).
 */
bool
BuildGlideConeGrid(const GlideConeGridRequest &request,
                   const RasterTerrain &terrain,
                   const Waypoints *waypoints,
                   GlideConePreparedGrid &out) noexcept;
