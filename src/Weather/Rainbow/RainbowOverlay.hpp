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

struct PreparedTile {
  AllocatedPath path;
  LayerId layer_id;
  GeoBitmap::TileData tile;
  int64_t snapshot = 0;
};

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

  [[nodiscard]] unsigned CountPlannedTiles() const noexcept;
};

[[nodiscard]]
OverlayPlan
BuildOverlayPlan(GlueMapWindow &map, bool satellite, bool rain,
                 int64_t snapshot_time) noexcept;

Co::Task<std::vector<PreparedTile>>
DownloadOverlayTiles(CurlGlobal &curl, std::string_view api_key,
                     OverlayPlan plan, ProgressListener &progress);

Co::Task<void>
CacheOverlayTiles(CurlGlobal &curl, std::string_view api_key,
                  OverlayPlan plan, ProgressListener &progress);

Co::Task<bool>
PrefetchHistorySnapshots(CurlGlobal &curl, std::string_view api_key,
                         OverlayPlan base_plan, int64_t reference,
                         ProgressListener &progress);

unsigned
InstallCachedOverlayTiles(const OverlayPlan &plan, int64_t snapshot,
                          bool allow_partial = false) noexcept;

unsigned
InstallPreparedOverlays(std::vector<PreparedTile> &&tiles) noexcept;

void
ClearOverlays() noexcept;

} // namespace Rainbow
