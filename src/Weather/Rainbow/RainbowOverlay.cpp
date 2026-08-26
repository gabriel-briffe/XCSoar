// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RainbowOverlay.hpp"
#include "FieldControls.hpp"
#include "RainbowAPI.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "MapWindow/OverlayBitmap.hpp"
#include "MapWindow/OverlayLimits.hpp"
#include "UIGlobals.hpp"
#include "thread/Debug.hpp"
#include "util/StaticString.hxx"
#include "Weather/SkySight/LiveTileUtils.hpp"
#include "ui/canvas/Bitmap.hpp"
#include "LogFile.hpp"
#include "system/FileUtil.hpp"

#include <algorithm>
#include <stdexcept>

namespace Rainbow {

#ifdef ENABLE_OPENGL

static void
ClearOverlayRange(GlueMapWindow &map, unsigned begin,
                  unsigned end) noexcept
{
  for (unsigned i = begin; i < end; ++i)
    map.SetOverlay(i, nullptr);
}

static std::vector<GeoBitmap::TileData>
PlanLayerTiles(GlueMapWindow &map, const GeoBounds &map_bounds,
               const LayerSpec &layer)
{
  const auto map_tile = GeoBitmap::GetTile(map.VisibleProjection(),
                                           layer.zoom_min,
                                           SkySight::GetLiveTileMapZoomMaximum(
                                             layer.zoom_max));
  const auto live_zoom = SkySight::SelectLiveTileZoom(map_tile.zoom,
                                                      layer.zoom_min);
  const auto base_tile = GeoBitmap::GetTile(map_bounds, live_zoom);
  return CollectVisibleTiles(map_bounds, base_tile, TILE_RANGE);
}

[[nodiscard]]
static const LayerSpec &
LayerSpecFor(LayerId id) noexcept
{
  return id == LayerId::RAIN ? LAYER_RAIN : LAYER_SATELLITE;
}

static bool
SetOverlayTile(GlueMapWindow &map, unsigned slot, Path path,
               const LayerSpec &layer, const GeoBitmap::TileData &tile)
{
  /* Prefer Bitmap + explicit geo bounds so WebP cloud tiles (Android
     BitmapFactory) and PNG rain tiles both work — LoadGeoFile only
     accepts PNG/JPEG by extension. */
  Bitmap bitmap;
  if (!bitmap.LoadFile(path)) {
    LogFmt("rainbow: failed to load tile {}", path.c_str());
    return false;
  }

  StaticString<160> label;
  label.Format("Rainbow: %s (%u/%u/%u)",
               layer.file_prefix, tile.zoom, tile.x, tile.y);

  auto overlay = std::make_unique<MapOverlayBitmap>(
    std::move(bitmap), GeoBitmap::GetGeoQuadrilateral(tile),
    label.c_str());
  overlay->SetAlpha(layer.alpha);
  map.SetOverlay(slot, std::move(overlay));
  return true;
}

static Co::Task<void>
DownloadLayerTiles(CurlGlobal &curl, std::string_view api_key,
                   Path cache_dir, const LayerSpec &layer,
                   const std::vector<GeoBitmap::TileData> &tiles,
                   int64_t snapshot_time, unsigned capacity,
                   std::vector<PreparedTile> &out,
                   ProgressListener &progress)
{
  if (tiles.empty() || capacity == 0)
    co_return;

  int64_t snapshot = snapshot_time;
  if (snapshot <= 0)
    snapshot = co_await FetchSnapshot(curl, api_key, layer.api_layer,
                                      progress);

  unsigned filled = 0;
  for (const auto &tile : tiles) {
    if (filled >= capacity)
      break;

    try {
      auto path = co_await EnsureTile(curl, api_key, cache_dir,
                                      layer, snapshot, tile, progress);
      out.push_back(PreparedTile{
        std::move(path),
        layer.id,
        tile,
        snapshot,
      });
      ++filled;
    } catch (...) {
      /* skip failed tiles; keep going for the rest of the viewport */
    }
  }
}

#endif

OverlayPlan
BuildOverlayPlan(GlueMapWindow &map, bool satellite, bool rain,
                 int64_t snapshot_time) noexcept
{
  OverlayPlan plan;
#ifndef ENABLE_OPENGL
  (void)map;
  (void)satellite;
  (void)rain;
  (void)snapshot_time;
  return plan;
#else
  assert(InMainThread());

  plan.map_bounds = map.VisibleProjection().GetScreenBounds();
  plan.satellite = satellite;
  plan.rain = rain;
  plan.snapshot_time = snapshot_time;

  if (!plan.map_bounds.Check() || !plan.map_bounds.IsValid())
    return plan;

  if (satellite)
    plan.satellite_tiles = PlanLayerTiles(map, plan.map_bounds,
                                          LAYER_SATELLITE);
  if (rain)
    plan.rain_tiles = PlanLayerTiles(map, plan.map_bounds, LAYER_RAIN);

  return plan;
#endif
}

unsigned
OverlayPlan::CountPlannedTiles() const noexcept
{
  const unsigned layer_count =
    (satellite ? 1u : 0u) + (rain ? 1u : 0u);
  if (layer_count == 0)
    return 0;

  const unsigned slots_per_layer =
    MapWindowOverlay::MAX_MAP_OVERLAYS / layer_count;
  unsigned n = 0;
  if (satellite)
    n += std::min(unsigned(satellite_tiles.size()), slots_per_layer);
  if (rain)
    n += std::min(unsigned(rain_tiles.size()), slots_per_layer);
  return n;
}

Co::Task<std::vector<PreparedTile>>
DownloadOverlayTiles(CurlGlobal &curl, std::string_view api_key,
                     OverlayPlan plan, ProgressListener &progress)
{
#ifndef ENABLE_OPENGL
  (void)curl;
  (void)api_key;
  (void)plan;
  (void)progress;
  throw std::runtime_error("Rainbow overlays require OpenGL");
#else
  if (!plan.IsValid())
    co_return std::vector<PreparedTile>{};

  const auto cache_dir = MakeCacheDirectory();
  const unsigned layer_count =
    (plan.satellite ? 1u : 0u) + (plan.rain ? 1u : 0u);
  const unsigned slots_per_layer =
    MapWindowOverlay::MAX_MAP_OVERLAYS / layer_count;

  std::vector<PreparedTile> prepared;
  prepared.reserve(MapWindowOverlay::MAX_MAP_OVERLAYS);

  if (plan.satellite)
    co_await DownloadLayerTiles(curl, api_key, cache_dir, LAYER_SATELLITE,
                                plan.satellite_tiles, plan.snapshot_time,
                                slots_per_layer, prepared, progress);

  if (plan.rain)
    co_await DownloadLayerTiles(curl, api_key, cache_dir, LAYER_RAIN,
                                plan.rain_tiles, plan.snapshot_time,
                                slots_per_layer, prepared, progress);

  co_return prepared;
#endif
}

Co::Task<void>
CacheOverlayTiles(CurlGlobal &curl, std::string_view api_key,
                  OverlayPlan plan, ProgressListener &progress)
{
#ifndef ENABLE_OPENGL
  (void)curl;
  (void)api_key;
  (void)plan;
  (void)progress;
  co_return;
#else
  if (!plan.IsValid())
    co_return;

  const auto cache_dir = MakeCacheDirectory();
  const unsigned layer_count =
    (plan.satellite ? 1u : 0u) + (plan.rain ? 1u : 0u);
  const unsigned slots_per_layer =
    MapWindowOverlay::MAX_MAP_OVERLAYS / layer_count;

  if (plan.satellite) {
    int64_t snapshot = plan.snapshot_time;
    if (snapshot <= 0)
      snapshot = co_await FetchSnapshot(curl, api_key,
                                      LAYER_SATELLITE.api_layer, progress);

    unsigned filled = 0;
    for (const auto &tile : plan.satellite_tiles) {
      if (filled >= slots_per_layer)
        break;
      try {
        (void)co_await EnsureTile(curl, api_key, cache_dir, LAYER_SATELLITE,
                                  snapshot, tile, progress);
      } catch (...) {
      }
      ++filled;
    }
  }

  if (plan.rain) {
    int64_t snapshot = plan.snapshot_time;
    if (snapshot <= 0)
      snapshot = co_await FetchSnapshot(curl, api_key,
                                      LAYER_RAIN.api_layer, progress);

    unsigned filled = 0;
    for (const auto &tile : plan.rain_tiles) {
      if (filled >= slots_per_layer)
        break;
      try {
        (void)co_await EnsureTile(curl, api_key, cache_dir, LAYER_RAIN,
                                  snapshot, tile, progress);
      } catch (...) {
      }
      ++filled;
    }
  }
#endif
}

Co::Task<bool>
PrefetchHistorySnapshots(CurlGlobal &curl, std::string_view api_key,
                         OverlayPlan base_plan, int64_t reference,
                         ProgressListener &progress)
{
#ifndef ENABLE_OPENGL
  (void)curl;
  (void)api_key;
  (void)base_plan;
  (void)reference;
  (void)progress;
  co_return false;
#else
  static constexpr unsigned PREFETCH_STEPS = 2;

  for (unsigned step = 1; step <= PREFETCH_STEPS; ++step) {
    const int64_t snapshot =
      reference - int64_t(step) * SNAPSHOT_INTERVAL_SECONDS;
    if (snapshot <= 0 ||
        reference - snapshot > SNAPSHOT_HISTORY_SECONDS)
      continue;

    auto plan = base_plan;
    plan.snapshot_time = snapshot;
    co_await CacheOverlayTiles(curl, api_key, std::move(plan), progress);
  }

  co_return true;
#endif
}

unsigned
InstallCachedOverlayTiles(const OverlayPlan &plan,
                          int64_t snapshot) noexcept
{
#ifndef ENABLE_OPENGL
  (void)plan;
  (void)snapshot;
  return 0;
#else
  if (snapshot <= 0 || !plan.IsValid())
    return 0;

  const auto cache_dir = MakeCacheDirectory();
  const unsigned layer_count =
    (plan.satellite ? 1u : 0u) + (plan.rain ? 1u : 0u);
  const unsigned slots_per_layer =
    MapWindowOverlay::MAX_MAP_OVERLAYS / layer_count;

  std::vector<PreparedTile> prepared;
  prepared.reserve(MapWindowOverlay::MAX_MAP_OVERLAYS);

  const auto collect = [&](const LayerSpec &layer,
                           const std::vector<GeoBitmap::TileData> &tiles)
    -> bool
  {
    unsigned filled = 0;
    for (const auto &tile : tiles) {
      if (filled >= slots_per_layer)
        break;

      auto path = MakeTilePath(cache_dir, layer, snapshot, tile);
      if (!File::ExistsAny(path))
        return false;

      prepared.push_back(PreparedTile{
        std::move(path),
        layer.id,
        tile,
        snapshot,
      });
      ++filled;
    }
    return true;
  };

  if (plan.satellite &&
      !collect(LAYER_SATELLITE, plan.satellite_tiles))
    return 0;
  if (plan.rain &&
      !collect(LAYER_RAIN, plan.rain_tiles))
    return 0;

  return InstallPreparedOverlays(std::move(prepared));
#endif
}

unsigned
InstallPreparedOverlays(std::vector<PreparedTile> &&tiles) noexcept
{
#ifndef ENABLE_OPENGL
  (void)tiles;
  return 0;
#else
  assert(InMainThread());

  auto *map = UIGlobals::GetMapIfActive();
  if (map == nullptr)
    return 0;

  ClearOverlays();

  unsigned filled = 0;
  for (auto &tile : tiles) {
    if (filled >= MapWindowOverlay::MAX_MAP_OVERLAYS)
      break;

    const auto &layer = LayerSpecFor(tile.layer_id);
    if (SetOverlayTile(*map, filled, tile.path, layer, tile.tile))
      ++filled;
  }

  return filled;
#endif
}

void
ClearOverlays() noexcept
{
#ifdef ENABLE_OPENGL
  assert(InMainThread());

  auto *map = UIGlobals::GetMap();
  if (map == nullptr)
    return;

  ClearOverlayRange(*map, 0, MapWindowOverlay::MAX_MAP_OVERLAYS);
#endif
}

} // namespace Rainbow
