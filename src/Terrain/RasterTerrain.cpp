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
    if (LoadCache(cache, path))
      return;
  } catch (...) {
    LogError(std::current_exception(), "Failed to load terrain cache");
  }

  LoadTerrainOverview(archive.get(), map.GetTileCache(), operation);

  map.UpdateProjection();

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

  DemOverview::Lod lod = DemOverview::Lod::FINE;
  if (meters_per_screen_pixel > 0) {
    const double cell_m = map.PixelDistance(location, 1);
    const double cpp = cell_m > 0
      ? meters_per_screen_pixel / cell_m
      : 0;
    lod = DemOverview::Select(cpp, scale_bar_meters,
                              dem_medium_scale, dem_coarse_scale);

    /* HD needs every fine tile under the view.  If that exceeds the
       active-tile budget, fall back to the medium overview so the
       whole screen stays consistent (no partial HD window). */
    if (lod == DemOverview::Lod::FINE) {
      const auto &projection = map.GetProjection();
      const auto raster_location = projection.ProjectCoarse(location);
      const unsigned radius_px =
        projection.DistancePixelsCoarse(radius);
      if (tile_cache.ExceedsActiveTileBudget(raster_location, radius_px))
        lod = DemOverview::Lod::MEDIUM;
    }
  }

  tile_cache.SetDisplayLod(lod);

  if (lod != DemOverview::Lod::FINE) {
    /* Overview-only display: drop fine tiles so mid/far zoom stays
       light.  Height queries fall back to the coarse overview. */
    tile_cache.UnloadTiles();
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
