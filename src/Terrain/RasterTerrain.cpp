// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RasterTerrain.hpp"
#include "Loader.hpp"
#include "DemOverview.hpp"
#include "Profile/Profile.hpp"
#include "io/ZipArchive.hpp"
#include "io/FileCache.hpp"
#include "io/FileOutputStream.hxx"
#include "io/BufferedOutputStream.hxx"
#include "io/Reader.hxx"
#include "io/BufferedReader.hxx"
#include "system/ConvertPathName.hpp"
#include "Operation/Operation.hpp"
#include "LogFile.hpp"

static const char *const terrain_cache_name = "terrain";

static void
LogDemSizes(const RasterMap &map) noexcept
{
  const auto &cache = map.GetTileCache();
  if (!cache.IsValid())
    return;

  const auto fine = cache.GetSize();
  const auto medium = cache.GetOverviewMedium().GetSize();
  const auto coarse = cache.GetOverview().GetSize();
  constexpr double bytes_per_sample = sizeof(TerrainHeight);

  const auto mib = [](unsigned w, unsigned h) noexcept {
    return double(w) * double(h) * bytes_per_sample / (1024. * 1024.);
  };

  LogFmt("Terrain DEM 1 (HD): {}x{} ({:.1f} MiB)",
         fine.x, fine.y, mib(fine.x, fine.y));
  LogFmt("Terrain DEM 2 (medium): {}x{} ({:.1f} MiB)",
         medium.x, medium.y, mib(medium.x, medium.y));
  LogFmt("Terrain DEM 3 (coarse): {}x{} ({:.1f} MiB)",
         coarse.x, coarse.y, mib(coarse.x, coarse.y));
}

inline bool
RasterTerrain::LoadCache(FileCache &cache, Path path)
{
  auto r = cache.Load(terrain_cache_name, path);
  if (!r)
    return false;

  BufferedReader br(*r);
  map.LoadCache(br);
  return true;
}

inline bool
RasterTerrain::LoadCache(FileCache *cache, Path path)
{
  return cache != nullptr && LoadCache(*cache, path);
}

inline void
RasterTerrain::SaveCache(FileCache &cache, Path path) const
{
  auto os = cache.Save(terrain_cache_name, path);
  BufferedOutputStream bos(*os);
  map.SaveCache(bos);
  bos.Flush();
  os->Commit();
}

inline void
RasterTerrain::Load(Path path, FileCache *cache,
                    OperationEnvironment &operation)
{
  try {
    if (LoadCache(cache, path)) {
      LogDemSizes(map);
      return;
    }
  } catch (...) {
    LogError(std::current_exception(), "Failed to load terrain cache");
  }

  LoadTerrainOverview(archive.get(), map.GetTileCache(), operation);

  map.UpdateProjection();
  LogDemSizes(map);

  if (cache != nullptr) {
    try {
      SaveCache(*cache, path);
    } catch (...) {
      LogError(std::current_exception(), "Failed to save terrain cache");
    }
  }
}

std::unique_ptr<RasterTerrain>
RasterTerrain::OpenTerrain(FileCache *cache, Path path,
                           OperationEnvironment &operation)
{
  auto rt = std::make_unique<RasterTerrain>(ZipArchive{path});
  rt->Load(path, cache, operation);
  return rt;
}

std::unique_ptr<RasterTerrain>
RasterTerrain::OpenTerrain(FileCache *cache, OperationEnvironment &operation)
try {
  const auto path = Profile::GetPath(ProfileKeys::MapFile);
  if (path == nullptr)
    return nullptr;

  return OpenTerrain(cache, path, operation);
} catch (...) {
  operation.SetError(std::current_exception());
  return nullptr;
}

bool
RasterTerrain::UpdateTiles(const GeoPoint &location, double radius,
                           double meters_per_screen_pixel,
                           double scale_bar_meters,
                           double dem_medium_scale,
                           double dem_coarse_scale) noexcept
{
  auto &tile_cache = map.GetTileCache();
  if (!tile_cache.IsValid())
    return false;

  const auto &projection = map.GetProjection();
  const auto raster_location = projection.ProjectCoarse(location);
  const unsigned radius_px = projection.DistancePixelsCoarse(radius);

  DemOverview::Lod lod;
  if (dem_medium_scale <= 0 && dem_coarse_scale <= 0) {
    /* Auto: ignore cells-per-pixel.  Same rule at each step — if the
       view needs more JP2 tiles than this LOD's budget, step up. */
    lod = tile_cache.SelectLodByTileBudget(raster_location, radius_px);
  } else {
    const double cell_m = map.PixelDistance(location, 1);
    const double cpp = cell_m > 0 && meters_per_screen_pixel > 0
      ? meters_per_screen_pixel / cell_m
      : 0;
    lod = DemOverview::Select(cpp, scale_bar_meters,
                              dem_medium_scale, dem_coarse_scale);

    /* Manual thresholds can still leave FINE/MEDIUM short of the
       screen; promote with the same tile-budget rule (≤ MAX tiles
       of the current LOD). */
    if (lod == DemOverview::Lod::FINE &&
        tile_cache.ExceedsActiveTileBudget(raster_location, radius_px,
                                           DemOverview::Lod::FINE))
      lod = DemOverview::Lod::MEDIUM;
    if (lod == DemOverview::Lod::MEDIUM &&
        tile_cache.ExceedsActiveTileBudget(raster_location, radius_px,
                                           DemOverview::Lod::MEDIUM))
      lod = DemOverview::Lod::COARSE;
  }

  tile_cache.SetDisplayLod(lod);

  if (lod != DemOverview::Lod::FINE) {
    /* Overview LOD: refresh the active tile set for GPU crops; do not
       decode JP2. */
    const std::lock_guard lock{mutex};
    tile_cache.PollTiles(raster_location, radius_px, false);
    return false;
  }

  try {
    UpdateTerrainTiles(archive.get(), tile_cache, mutex,
                       map.GetProjection(), location, radius);
  } catch (...) {
    LogError(std::current_exception(), "Failed to update terrain tiles");
  }

  return map.IsDirty();
}
