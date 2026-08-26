// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "ui/canvas/custom/GeoBitmap.hpp"
#include "system/Path.hpp"
#include "co/Task.hxx"

#include <string>
#include <string_view>
#include <vector>

class CurlGlobal;
class ProgressListener;

namespace Rainbow {

enum class LayerId {
  SATELLITE,
  RAIN,
};

struct LayerSpec {
  LayerId id;
  /** Filename prefix recognised by Bitmap::LoadGeoFile ParseTileBounds. */
  const char *file_prefix;
  /** Rainbow snapshot/tile layer query name. */
  const char *api_layer;
  float alpha;
  unsigned zoom_min;
  unsigned zoom_max;
};

inline constexpr LayerSpec LAYER_SATELLITE{
  LayerId::SATELLITE, "satellite", "clouds", 1.0f, 1, 7,
};

inline constexpr LayerSpec LAYER_RAIN{
  LayerId::RAIN, "rain", "precip", 0.7f, 1, 12,
};

/**
 * Rainbow precip tile color query value (see doc.rainbow.ai/tile_colors).
 * 8 = RainViewer Universal Blue / Original.
 */
inline constexpr unsigned PRECIP_COLOR_PALETTE = 8;

inline constexpr unsigned TILE_RANGE = 1; /* 3×3 tiles per layer */

AllocatedPath
MakeCacheDirectory();

std::string
MakeSnapshotUrl(std::string_view api_key, std::string_view api_layer);

std::string
MakeTileUrl(std::string_view api_key, const LayerSpec &layer,
            int64_t snapshot, const GeoBitmap::TileData &tile);

AllocatedPath
MakeTilePath(Path cache_dir, const LayerSpec &layer, int64_t snapshot,
             const GeoBitmap::TileData &tile);

/**
 * Fetch the latest snapshot epoch for @p api_layer.
 * Throws on HTTP/JSON errors.
 */
Co::Task<int64_t>
FetchSnapshot(CurlGlobal &curl, std::string_view api_key,
              std::string_view api_layer, ProgressListener &progress);

/**
 * Download one PNG tile if missing.  Throws on HTTP errors.
 */
Co::Task<AllocatedPath>
EnsureTile(CurlGlobal &curl, std::string_view api_key, Path cache_dir,
           const LayerSpec &layer, int64_t snapshot,
           const GeoBitmap::TileData &tile, ProgressListener &progress);

std::vector<GeoBitmap::TileData>
CollectVisibleTiles(const GeoBounds &map_bounds,
                    const GeoBitmap::TileData &base_tile,
                    unsigned range);

} // namespace Rainbow
