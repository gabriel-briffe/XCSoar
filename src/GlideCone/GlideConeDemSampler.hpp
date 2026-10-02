// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Terrain/RasterMap.hpp"
#include "Terrain/DemOverview.hpp"
#include "Geo/GeoPoint.hpp"
#include "io/ZipArchive.hpp"
#include "thread/SharedMutex.hpp"
#include "system/Path.hpp"

#include <memory>
#include <optional>

class RasterTerrain;

/**
 * GlideCone-only DEM access: own archive + tile set, independent of
 * the map-display #RasterTerrain.  Layout/overview is copied from the
 * display DEM when available (no second file overview scan); fine
 * tiles are ephemeral (load → sample → unload) when DEM1 is needed.
 */
class GlideConeDemSampler {
  std::optional<ZipArchive> archive;
  AllocatedPath path;
  SharedMutex mutex;
  RasterMap map;
  bool overview_ready = false;
  /** Last cell-size (m) for which DEM choice was logged; <0 = none. */
  double logged_cell_size_m = -1;

public:
  /**
   * Read-only lease on the sampler map.  Caller must hold tiles loaded
   * via #UpdateTiles for the window of interest when using DEM1.
   */
  class Lease {
    GlideConeDemSampler &sampler;

  public:
    explicit Lease(GlideConeDemSampler &_sampler) noexcept
      :sampler(_sampler) {
      sampler.mutex.lock_shared();
    }

    Lease(const Lease &) = delete;

    ~Lease() noexcept {
      sampler.mutex.unlock_shared();
    }

    operator const RasterMap &() const noexcept {
      return sampler.map;
    }

    const RasterMap *operator->() const noexcept {
      return &sampler.map;
    }
  };

  /**
   * Open the configured map file (if needed) and obtain layout
   * metadata.  Prefers copying from @p display; falls back to a quiet
   * overview load from the zip.
   *
   * @return false if no map file or overview/layout unavailable
   */
  bool EnsureOverview(RasterTerrain *display) noexcept;

  /**
   * Pick DEM1/2/3 from glide-cone cell size vs fine DEM spacing, set
   * the private cache LOD, and load JP2 only for DEM1.
   *
   * Logs once whenever @p cell_size_m changes.
   *
   * @return false if overview unavailable
   */
  bool Prepare(RasterTerrain *display, const GeoPoint &location,
               double radius, double cell_size_m) noexcept;

  /**
   * Load fine tiles for @p location / @p radius into the private cache.
   *
   * @return true if more rounds are needed (same as
   *         #RasterTerrain::UpdateTiles)
   */
  bool UpdateTiles(RasterTerrain *display,
                   const GeoPoint &location, double radius) noexcept;

  /** Drop fine tiles; keep overview for the next build. */
  void ReleaseTiles() noexcept;
};
