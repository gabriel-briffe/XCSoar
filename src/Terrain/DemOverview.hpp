// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Height.hpp"
#include "RasterTraits.hpp"

#include <algorithm>
#include <cstdint>

/**
 * DEM display level-of-detail: full-resolution tiles, 4× overview, or
 * 16× overview.  Selection is based on fine DEM cells per screen pixel.
 */
namespace DemOverview {

enum class Lod : uint8_t {
  FINE,
  MEDIUM,
  COARSE,
};

/**
 * Pick a LOD from fine DEM cells covered by one screen pixel.
 * Thresholds match the overview pool sizes (4 and 16).
 */
[[gnu::const]]
constexpr Lod
Select(double cells_per_pixel) noexcept
{
  if (cells_per_pixel >= double(1u << RasterTraits::OVERVIEW_BITS))
    return Lod::COARSE;
  if (cells_per_pixel >= double(1u << RasterTraits::OVERVIEW_MEDIUM_BITS))
    return Lod::MEDIUM;
  return Lod::FINE;
}

/**
 * Pick a LOD using optional map-scale-bar thresholds (metres).
 * A threshold of 0 means Auto for that step.
 *
 * When both thresholds are 0, map display does not use this helper —
 * #RasterTileCache::SelectLodByTileBudget picks LOD from the JP2 tile
 * budget instead.  This function remains for mixed/manual thresholds.
 */
[[gnu::const]]
constexpr Lod
Select(double cells_per_pixel, double scale_bar_m,
       double medium_scale_m, double coarse_scale_m) noexcept
{
  if (medium_scale_m <= 0 && coarse_scale_m <= 0)
    return Select(cells_per_pixel);

  if (medium_scale_m > 0 && coarse_scale_m > 0) {
    if (scale_bar_m >= coarse_scale_m)
      return Lod::COARSE;
    if (scale_bar_m >= medium_scale_m)
      return Lod::MEDIUM;
    return Lod::FINE;
  }

  Lod lod = Lod::FINE;
  if (medium_scale_m > 0) {
    if (scale_bar_m >= medium_scale_m)
      lod = Lod::MEDIUM;
  } else {
    lod = Select(cells_per_pixel);
    if (lod == Lod::COARSE)
      lod = Lod::MEDIUM;
  }

  if (coarse_scale_m > 0) {
    if (scale_bar_m >= coarse_scale_m)
      lod = Lod::COARSE;
  } else if (Select(cells_per_pixel) == Lod::COARSE) {
    lod = Lod::COARSE;
  }

  return lod;
}

[[gnu::const]]
constexpr unsigned
Bits(Lod lod) noexcept
{
  switch (lod) {
  case Lod::MEDIUM:
    return RasterTraits::OVERVIEW_MEDIUM_BITS;
  case Lod::COARSE:
    return RasterTraits::OVERVIEW_BITS;
  case Lod::FINE:
    return 0;
  }

  return 0;
}

/**
 * Max-pool one @p pool × @p pool block from a row-major height grid.
 * Water counts as 0 m; invalid samples are skipped.  Returns Invalid
 * when the block has no valid sample.
 */
[[gnu::pure]]
inline TerrainHeight
MaxPool(const TerrainHeight *src, unsigned src_w, unsigned src_h,
        unsigned x0, unsigned y0, unsigned pool) noexcept
{
  bool any = false;
  int16_t max_h = 0;

  const unsigned x1 = std::min(x0 + pool, src_w);
  const unsigned y1 = std::min(y0 + pool, src_h);

  for (unsigned y = y0; y < y1; ++y) {
    const TerrainHeight *row = src + y * src_w;
    for (unsigned x = x0; x < x1; ++x) {
      const TerrainHeight h = row[x];
      if (h.IsInvalid())
        continue;

      const int16_t v = h.IsWater() ? int16_t(0) : h.GetValue();
      if (!any || v > max_h)
        max_h = v;
      any = true;
    }
  }

  return any ? TerrainHeight(max_h) : TerrainHeight::Invalid();
}

/**
 * Build a max-pooled overview of @p src into @p dest.
 * @p dest must hold ToOverviewCeil(src_w/h) samples for @p bits.
 */
inline void
Build(const TerrainHeight *src, unsigned src_w, unsigned src_h,
      TerrainHeight *dest, unsigned bits) noexcept
{
  const unsigned pool = 1u << bits;
  const unsigned dw = RasterTraits::ToOverviewCeil(src_w, bits);
  const unsigned dh = RasterTraits::ToOverviewCeil(src_h, bits);

  for (unsigned y = 0; y < dh; ++y)
    for (unsigned x = 0; x < dw; ++x)
      dest[y * dw + x] =
        MaxPool(src, src_w, src_h, x * pool, y * pool, pool);
}

} // namespace DemOverview
