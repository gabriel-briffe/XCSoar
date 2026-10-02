// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeDemSampler.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Terrain/Loader.hpp"
#include "Profile/Profile.hpp"
#include "Operation/Operation.hpp"
#include "LogFile.hpp"

#include <cmath>

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

    return overview_ready;
  } catch (...) {
    overview_ready = false;
    archive.reset();
    return false;
  }
}

bool
GlideConeDemSampler::Prepare(RasterTerrain *display,
                             const GeoPoint &location,
                             double radius,
                             double cell_size_m) noexcept
{
  if (!EnsureOverview(display))
    return false;

  double dem_ref = 1;
  {
    const std::lock_guard lock{mutex};
    if (!map.IsDefined())
      return false;

    double dem_x = map.PixelDistanceX(location, 1);
    double dem_y = map.PixelDistanceY(location, 1);
    if (dem_x < 1)
      dem_x = 1;
    if (dem_y < 1)
      dem_y = 1;
    dem_ref = std::sqrt(dem_x * dem_y);
  }

  const double cpp = cell_size_m > 0 ? cell_size_m / dem_ref : 0;
  const auto lod = DemOverview::Select(cpp);

  {
    const std::lock_guard lock{mutex};
    map.GetTileCache().SetDisplayLod(lod);
  }

  if (cell_size_m != logged_cell_size_m) {
    logged_cell_size_m = cell_size_m;
    const unsigned dem_n = lod == DemOverview::Lod::FINE ? 1
      : (lod == DemOverview::Lod::MEDIUM ? 2 : 3);
    LogFmt("GlideCone: DEM{} for cell size {:.0f} m", dem_n, cell_size_m);
  }

  if (lod == DemOverview::Lod::FINE) {
    for (unsigned i = 0; i < 64 && UpdateTiles(display, location, radius);
         ++i) {
    }
  } else {
    /* Overview already resident; drop any leftover HD tiles. */
    ReleaseTiles();
  }

  return true;
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
  }

  return map.IsDirty();
}

void
GlideConeDemSampler::ReleaseTiles() noexcept
{
  const std::lock_guard lock{mutex};
  map.GetTileCache().UnloadTiles();
}
