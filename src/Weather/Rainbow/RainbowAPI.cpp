// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RainbowAPI.hpp"
#include "LocalPath.hpp"
#include "LogFile.hpp"
#include "lib/curl/CoRequest.hxx"
#include "lib/curl/Setup.hxx"
#include "net/http/CoDownloadToFile.hpp"
#include "net/http/Progress.hpp"
#include "system/FileUtil.hpp"
#include "util/StaticString.hxx"
#include "Geo/GeoBounds.hpp"

#include <boost/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace Rainbow {

AllocatedPath
MakeCacheDirectory()
{
  const auto weather_path = LocalPath("weather");
  Directory::Create(weather_path);
  auto rainbow_path = AllocatedPath::Build(weather_path, Path("rainbow"));
  Directory::Create(rainbow_path);
  return rainbow_path;
}

std::string
MakeSnapshotUrl(std::string_view api_key, std::string_view api_layer)
{
  StaticString<256> url;
  url.Format("https://api.rainbow.ai/tiles/v1/snapshot?layer=%.*s&token=%.*s",
             int(api_layer.size()), api_layer.data(),
             int(api_key.size()), api_key.data());
  return url.c_str();
}

std::string
MakeTileUrl(std::string_view api_key, const LayerSpec &layer,
            int64_t snapshot, const GeoBitmap::TileData &tile)
{
  StaticString<384> url;
  if (layer.id == LayerId::RAIN)
    /* forecast_time 0 = latest observation for this snapshot;
       color = RainViewer Universal Blue palette */
    url.Format("https://api.rainbow.ai/tiles/v1/precip/%lld/0/%u/%u/%u"
               "?color=%u&token=%.*s",
               static_cast<long long>(snapshot),
               tile.zoom, tile.x, tile.y,
               PRECIP_COLOR_PALETTE,
               int(api_key.size()), api_key.data());
  else
    url.Format("https://api.rainbow.ai/tiles/v1/clouds/%lld/%u/%u/%u"
               "?token=%.*s",
               static_cast<long long>(snapshot),
               tile.zoom, tile.x, tile.y,
               int(api_key.size()), api_key.data());
  return url.c_str();
}

AllocatedPath
MakeTilePath(Path cache_dir, const LayerSpec &layer, int64_t snapshot,
             const GeoBitmap::TileData &tile)
{
  StaticString<128> filename;
  /* Clouds tiles are WebP; precip tiles are PNG.  Extensions must match
     so Android BitmapFactory / LoadFile can decode them.  Include the
     precip palette id in the name so a palette change does not reuse
     old cached colours. */
  if (layer.id == LayerId::SATELLITE)
    filename.Format("%s-%u-%u-%u-%lld.webp",
                    layer.file_prefix, tile.zoom, tile.x, tile.y,
                    static_cast<long long>(snapshot));
  else
    filename.Format("%s-c%u-%u-%u-%u-%lld.png",
                    layer.file_prefix, PRECIP_COLOR_PALETTE,
                    tile.zoom, tile.x, tile.y,
                    static_cast<long long>(snapshot));
  return AllocatedPath::Build(cache_dir, Path(filename.c_str()));
}

Co::Task<int64_t>
FetchSnapshot(CurlGlobal &curl, std::string_view api_key,
              std::string_view api_layer, ProgressListener &progress)
{
  const auto url = MakeSnapshotUrl(api_key, api_layer);
  CurlEasy easy{url.c_str()};
  Curl::Setup(easy);
  const Net::ProgressAdapter progress_adapter{easy, progress};
  easy.SetFailOnError(false);

  const auto response = co_await Curl::CoRequest(curl, std::move(easy));
  if (response.status != 200)
    throw std::runtime_error("Rainbow snapshot HTTP error");

  const auto json = boost::json::parse(response.body);
  if (!json.is_object() || !json.as_object().contains("snapshot"))
    throw std::runtime_error("Rainbow snapshot JSON error");

  co_return json.at("snapshot").to_number<int64_t>();
}

Co::Task<AllocatedPath>
EnsureTile(CurlGlobal &curl, std::string_view api_key, Path cache_dir,
           const LayerSpec &layer, int64_t snapshot,
           const GeoBitmap::TileData &tile, ProgressListener &progress)
{
  auto path = MakeTilePath(cache_dir, layer, snapshot, tile);
  if (!File::ExistsAny(path)) {
    const auto url = MakeTileUrl(api_key, layer, snapshot, tile);
    LogFmt("rainbow: download {} {}/{}/{}",
           layer.file_prefix, tile.zoom, tile.x, tile.y);
    (void)co_await Net::CoDownloadToFile(curl, url.c_str(),
                                         nullptr, nullptr,
                                         path, nullptr, progress);
  }
  co_return path;
}

struct PrioritizedTile {
  GeoBitmap::TileData tile;
  unsigned priority;
};

std::vector<GeoBitmap::TileData>
CollectVisibleTiles(const GeoBounds &map_bounds,
                    const GeoBitmap::TileData &base_tile,
                    unsigned range)
{
  const int tiles_per_axis = 1 << base_tile.zoom;
  const auto normalize_x = [tiles_per_axis](int value) {
    int result = value % tiles_per_axis;
    if (result < 0)
      result += tiles_per_axis;
    return uint32_t(result);
  };

  std::vector<PrioritizedTile> candidates;
  const int tile_range = int(range);
  candidates.reserve(std::size_t(2 * tile_range + 1) *
                     std::size_t(2 * tile_range + 1));

  for (int dx = -tile_range; dx <= tile_range; ++dx) {
    for (int dy = -tile_range; dy <= tile_range; ++dy) {
      const int y = int(base_tile.y) + dy;
      if (y < 0 || y >= tiles_per_axis)
        continue;

      const GeoBitmap::TileData tile{
        base_tile.zoom,
        normalize_x(int(base_tile.x) + dx),
        uint32_t(y),
      };
      if (!GeoBitmap::GetBounds(tile).Overlaps(map_bounds))
        continue;

      candidates.push_back({tile, unsigned(dx * dx + dy * dy)});
    }
  }

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const auto &a, const auto &b) {
                     return a.priority < b.priority;
                   });

  std::vector<GeoBitmap::TileData> tiles;
  tiles.reserve(candidates.size());
  for (const auto &candidate : candidates)
    tiles.push_back(candidate.tile);
  return tiles;
}

} // namespace Rainbow
