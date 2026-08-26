// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "RainbowAPI.hpp"
#include "Geo/GeoBounds.hpp"
#include "co/Task.hxx"
#include "system/Path.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

class CurlGlobal;
class GlueMapWindow;
class ProgressListener;

namespace Rainbow {

/**
 * One downloaded tile ready to be installed on the OpenGL/UI thread.
 * Produced on the network thread; must not touch #Bitmap / overlays.
 */
struct PreparedTile {
  AllocatedPath path;
  LayerId layer_id;
  GeoBitmap::TileData tile;
  /** Snapshot epoch used for this tile (API or requested time). */
  int64_t snapshot = 0;
};

/**
 * Viewport snapshot taken on the UI thread before download starts.
 */
struct OverlayPlan {
  GeoBounds map_bounds = GeoBounds::Invalid();
  bool satellite = false;
  bool rain = false;
  int64_t snapshot_time = 0;
  std::vector<GeoBitmap::TileData> satellite_tiles;
  std::vector<GeoBitmap::TileData> rain_tiles;

  [[nodiscard]] bool IsValid() const noexcept {
    return map_bounds.Check() && map_bounds.IsValid() &&
      (satellite || rain);
  }

  /**
   * How many tiles #DownloadOverlayTiles will attempt (after the
   * per-layer overlay slot budget).
   */
  [[nodiscard]] unsigned CountPlannedTiles() const noexcept;
};

/**
 * Build a download plan from the current map viewport (UI thread).
 */
[[nodiscard]]
OverlayPlan
BuildOverlayPlan(GlueMapWindow &map, bool satellite, bool rain,
                 int64_t snapshot_time) noexcept;

/**
 * Download tiles for @p plan.  Runs on the network thread — no OpenGL.
 * Takes @p plan by value so the UI-thread snapshot outlives the job.
 */
Co::Task<std::vector<PreparedTile>>
DownloadOverlayTiles(CurlGlobal &curl, std::string_view api_key,
                     OverlayPlan plan, ProgressListener &progress);

/**
 * Load textures and install overlays.  Must run on the OpenGL/UI thread.
 *
 * @return number of overlay slots filled
 */
unsigned
InstallPreparedOverlays(std::vector<PreparedTile> &&tiles) noexcept;

/** Clear all map overlay slots used by Rainbow (UI/OpenGL thread). */
void
ClearOverlays() noexcept;

} // namespace Rainbow
