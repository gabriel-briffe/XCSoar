// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeDemSampler.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Terrain/Loader.hpp"
#include "Profile/Profile.hpp"
#include "Operation/Operation.hpp"
#include "LogFile.hpp"

bool
GlideConeDemSampler::EnsureOverview(RasterTerrain *display) noexcept
{
  try {
    auto map_path = Profile::GetPath(ProfileKeys::MapFile);
    if (map_path == nullptr)
      return false;

    const bool path_changed = !overview_ready || path != map_path;
    if (!path_changed)
      return overview_ready;

    path = std::move(map_path);
    archive.emplace(ZipArchive{path});
    map.GetTileCache().Reset();
    overview_ready = false;

    if (display != nullptr) {
      const RasterTerrain::Lease lease{*display};
      if (lease->IsDefined()) {
        map.GetTileCache().CopyLayoutFrom(lease->GetTileCache());
        map.UpdateProjection();
        overview_ready = map.IsDefined();
        if (overview_ready)
          return true;
      }
    }

    NullOperationEnvironment env;
    LoadTerrainOverview(archive->get(), map.GetTileCache(), env);
    map.UpdateProjection();
    overview_ready = map.IsDefined();
    if (!overview_ready)
      LogFmt("glidecones: dem: overview load failed");

    return overview_ready;
  } catch (...) {
    LogError(std::current_exception(), "GlideCone DEM overview failed");
    overview_ready = false;
    archive.reset();
    return false;
  }
}

bool
GlideConeDemSampler::UpdateTiles(RasterTerrain *display,
                                 const GeoPoint &location,
                                 double radius) noexcept
{
  if (!EnsureOverview(display))
    return false;

  auto &tile_cache = map.GetTileCache();
  try {
    UpdateTerrainTiles(archive->get(), tile_cache, mutex,
                       map.GetProjection(), location, radius);
  } catch (...) {
    LogError(std::current_exception(), "GlideCone DEM tile update failed");
  }

  return map.IsDirty();
}

void
GlideConeDemSampler::ReleaseTiles() noexcept
{
  const std::lock_guard lock{mutex};
  map.GetTileCache().UnloadTiles();
}
