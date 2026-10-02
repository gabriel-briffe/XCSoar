// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RasterTileCache.hpp"
#include "DemOverview.hpp"
#include "Math/Angle.hpp"
#include "io/BufferedOutputStream.hxx"
#include "io/BufferedReader.hxx"
#include "util/SpanCast.hxx"

extern "C" {
#include "jasper/jas_seq.h"
}

#include <stdexcept>

#include <string.h>
#include <algorithm>

/**
 * Prefer the higher elevation; Invalid loses to any valid sample.
 * Water counts as 0 m (same as overview build).
 */
static TerrainHeight
MaxTerrainHeight(TerrainHeight a, TerrainHeight b) noexcept
{
  if (a.IsInvalid())
    return b;
  if (b.IsInvalid())
    return a;

  const int16_t va = a.IsWater() ? int16_t(0) : a.GetValue();
  const int16_t vb = b.IsWater() ? int16_t(0) : b.GetValue();
  return TerrainHeight(std::max(va, vb));
}

/**
 * Max-pool one JP2 tile into an overview buffer.  Pool windows are
 * aligned to the global fine grid; overlapping tile contributions
 * are merged with max (not last-writer-wins).
 */
static void
PutMaxPoolOverview(RasterBuffer &overview, unsigned bits,
                   RasterLocation start,
                   const struct jas_matrix &m) noexcept
{
  const unsigned pool = 1u << bits;
  const unsigned dest_pitch = overview.GetSize().x;

  const unsigned ostart_x = RasterTraits::ToOverview(start.x, bits);
  const unsigned ostart_y = RasterTraits::ToOverview(start.y, bits);

  if (ostart_x >= overview.GetSize().x || ostart_y >= overview.GetSize().y)
    return;

  /* Cover every overview cell that intersects this tile. */
  const unsigned oend_x =
    RasterTraits::ToOverviewCeil(start.x + unsigned(m.numcols_), bits);
  const unsigned oend_y =
    RasterTraits::ToOverviewCeil(start.y + unsigned(m.numrows_), bits);

  unsigned width = oend_x - ostart_x;
  if (ostart_x + width > overview.GetSize().x)
    width = overview.GetSize().x - ostart_x;
  unsigned height = oend_y - ostart_y;
  if (ostart_y + height > overview.GetSize().y)
    height = overview.GetSize().y - ostart_y;

  auto *gcc_restrict dest = overview.GetData()
    + ostart_y * dest_pitch + ostart_x;

  for (unsigned oy = 0; oy < height; ++oy, dest += dest_pitch) {
    const unsigned global_y0 = (ostart_y + oy) << bits;
    for (unsigned ox = 0; ox < width; ++ox) {
      const unsigned global_x0 = (ostart_x + ox) << bits;

      const int fy0 = int(global_y0) - int(start.y);
      const int fx0 = int(global_x0) - int(start.x);
      const int fy1 = fy0 + int(pool);
      const int fx1 = fx0 + int(pool);

      const int row0 = std::max(fy0, 0);
      const int col0 = std::max(fx0, 0);
      const int row1 = std::min(fy1, int(m.numrows_));
      const int col1 = std::min(fx1, int(m.numcols_));

      bool any = false;
      int16_t max_h = 0;

      for (int fy = row0; fy < row1; ++fy) {
        const jas_seqent_t *row = m.rows_[fy];
        for (int fx = col0; fx < col1; ++fx) {
          const TerrainHeight h{int16_t(row[fx])};
          if (h.IsInvalid())
            continue;

          const int16_t v = h.IsWater() ? int16_t(0) : h.GetValue();
          if (!any || v > max_h)
            max_h = v;
          any = true;
        }
      }

      if (!any)
        continue;

      dest[ox] = MaxTerrainHeight(dest[ox], TerrainHeight(max_h));
    }
  }
}

void
RasterTileCache::PutOverviewTile(unsigned index,
                                 RasterLocation start, RasterLocation end,
                                 const struct jas_matrix &m) noexcept
{
  tiles.GetLinear(index).Set(start, end);

  PutMaxPoolOverview(overview_medium, RasterTraits::OVERVIEW_MEDIUM_BITS,
                     start, m);
  PutMaxPoolOverview(overview, RasterTraits::OVERVIEW_BITS, start, m);
}

void
RasterTileCache::PutTileData(unsigned index,
                             const struct jas_matrix &m) noexcept
{
  auto &tile = tiles.GetLinear(index);
  if (!tile.IsRequested())
    return;

  tile.CopyFrom(m);
}

struct RTDistanceSort {
  const RasterTileCache &rtc;

  constexpr RTDistanceSort(RasterTileCache &_rtc) noexcept:rtc(_rtc) {}

  [[gnu::pure]]
  bool operator()(unsigned short ai, unsigned short bi) const noexcept {
    const RasterTile &a = rtc.tiles.GetLinear(ai);
    const RasterTile &b = rtc.tiles.GetLinear(bi);

    return a.GetDistance() < b.GetDistance();
  }
};

bool
RasterTileCache::PollTiles(SignedRasterLocation p, unsigned radius,
                           bool load_fine) noexcept
{
  /* tiles are usually 256 pixels wide; with a radius smaller than
     that, the (optimized) tile distance calculations may fail;
     additionally, this ensures that tiles which are slightly out of
     the screen will be loaded in advance */
  radius += 256;

  /**
   * Maximum number of tiles loaded at a time, to reduce system load
   * peaks.
   */
  constexpr unsigned MAX_ACTIVATE = MAX_ACTIVE_TILES > 32
    ? 16
    : MAX_ACTIVE_TILES / 2;

  /* query all tiles; all tiles which are either in range or already
     loaded are added to RequestTiles */

  request_tiles.clear();
  for (int i = tiles.GetSize() - 1; i >= 0 && !request_tiles.full(); --i)
    if (tiles.GetLinear(i).VisibilityChanged(p, radius))
      request_tiles.append(i);

  /* Cap the active set so each LOD keeps a full-screen cover within
     its budget.  HD uses MAX_ACTIVE_TILES; DEM2/3 use the scaled
     budgets from #MaxActiveTilesForLod (set via #SetDisplayLod). */
  const unsigned max_tiles = load_fine
    ? MAX_ACTIVE_TILES
    : MaxActiveTilesForLod(display_lod);
  if (request_tiles.size() > max_tiles) {
    const RTDistanceSort sort(*this);
    std::sort(request_tiles.begin(), request_tiles.end(), sort);

    for (unsigned i = max_tiles; i < request_tiles.size(); ++i) {
      RasterTile &tile = tiles.GetLinear(request_tiles[i]);
      tile.Unload();
      tile.ClearRequest();
    }

    request_tiles.shrink(max_tiles);
  }

  if (!load_fine) {
    /* Overview LOD: keep the active set for GPU crops, drop HD. */
    for (std::size_t i : request_tiles) {
      RasterTile &tile = tiles.GetLinear(i);
      tile.Unload();
      tile.ClearRequest();
    }
    dirty = false;
    ++serial;
    return false;
  }

  /* fill ActiveTiles and request new tiles */

  dirty = false;

  unsigned num_activate = 0;
  for (unsigned i = 0; i < request_tiles.size(); ++i) {
    RasterTile &tile = tiles.GetLinear(request_tiles[i]);
    if (tile.IsLoaded())
      continue;

    if (++num_activate <= MAX_ACTIVATE)
      /* request the tile in the current iteration */
      tile.SetRequest();
    else
      /* this tile will be loaded in the next iteration */
      dirty = true;
  }

  return num_activate > 0;
}

unsigned
RasterTileCache::CountTilesInView(SignedRasterLocation p,
                                  unsigned radius) const noexcept
{
  /* Match PollTiles(): expand so edge tiles of the view are included. */
  radius += 256;

  unsigned count = 0;
  for (unsigned i = 0; i < tiles.GetSize(); ++i) {
    const auto &tile = tiles.GetLinear(i);
    if (!tile.IsDefined())
      continue;

    if (tile.CalcDistanceTo(p) > radius)
      continue;

    ++count;
  }

  return count;
}

unsigned
RasterTileCache::CountLodTilesInView(SignedRasterLocation p,
                                     unsigned radius,
                                     DemOverview::Lod lod) const noexcept
{
  const unsigned n_jp2 = CountTilesInView(p, radius);
  const unsigned per = Jp2TilesPerLodTile(lod);
  return (n_jp2 + per - 1) / per;
}

bool
RasterTileCache::ExceedsActiveTileBudget(SignedRasterLocation p,
                                         unsigned radius,
                                         DemOverview::Lod lod) const noexcept
{
  return CountLodTilesInView(p, radius, lod) > MAX_ACTIVE_TILES;
}

DemOverview::Lod
RasterTileCache::SelectLodByTileBudget(SignedRasterLocation p,
                                       unsigned radius) const noexcept
{
  /* Same test at every level: ≤ MAX_ACTIVE_TILES tiles of this LOD. */
  if (!ExceedsActiveTileBudget(p, radius, DemOverview::Lod::FINE))
    return DemOverview::Lod::FINE;
  if (!ExceedsActiveTileBudget(p, radius, DemOverview::Lod::MEDIUM))
    return DemOverview::Lod::MEDIUM;
  return DemOverview::Lod::COARSE;
}

TerrainHeight
RasterTileCache::GetHeight(RasterLocation p) const noexcept
{
  if (p.x >= size.x || p.y >= size.y)
    // outside overall bounds
    return TerrainHeight::Invalid();

  const RasterTile &tile = tiles.Get(p.x / tile_size.x, p.y / tile_size.y);
  if (tile.IsLoaded())
    return tile.GetHeight(p);

  /* No fine tile: sample the overview matching #display_lod (glide
     cone / map underlay).  FINE with missing HD keeps the coarse
     overview fallback. */
  if (display_lod == DemOverview::Lod::MEDIUM &&
      overview_medium.IsDefined()) {
    const auto osz = overview_medium.GetSize();
    const unsigned ox = p.x >> RasterTraits::OVERVIEW_MEDIUM_BITS;
    const unsigned oy = p.y >> RasterTraits::OVERVIEW_MEDIUM_BITS;
    if (ox >= osz.x || oy >= osz.y)
      return TerrainHeight::Invalid();
    return overview_medium.Get({ox, oy});
  }

  return overview.GetInterpolated(p << (RasterTraits::SUBPIXEL_BITS -
                                        RasterTraits::OVERVIEW_BITS));
}

TerrainHeight
RasterTileCache::GetInterpolatedHeight(RasterLocation l) const noexcept
{
  if (l.x >= overview_size_fine.x || l.y >= overview_size_fine.y)
    // outside overall bounds
    return TerrainHeight::Invalid();

  const auto [px, ix] = RasterTraits::CalcSubpixel(l.x);
  const auto [py, iy] = RasterTraits::CalcSubpixel(l.y);

  const RasterTile &tile = tiles.Get(px / tile_size.x, py / tile_size.y);
  if (tile.IsLoaded())
    return tile.GetInterpolatedHeight(px, py, ix, iy);

  if (display_lod == DemOverview::Lod::MEDIUM &&
      overview_medium.IsDefined()) {
    const auto osz = overview_medium.GetSize();
    const unsigned ox = RasterTraits::ToOverview(l.x,
      RasterTraits::OVERVIEW_MEDIUM_BITS);
    const unsigned oy = RasterTraits::ToOverview(l.y,
      RasterTraits::OVERVIEW_MEDIUM_BITS);
    if (ox >= osz.x || oy >= osz.y)
      return TerrainHeight::Invalid();
    return overview_medium.GetInterpolated({ox, oy});
  }

  // still not found, so go to overview
  return overview.GetInterpolated({RasterTraits::ToOverview(l.x),
                                   RasterTraits::ToOverview(l.y)});
}

void
RasterTileCache::SetSize(UnsignedPoint2D _size,
                         Point2D<uint_least16_t> _tile_size,
                         UnsignedPoint2D _n_tiles) noexcept
{
  size = _size;
  tile_size = _tile_size;

  /* round the overview size up, because PutOverviewTile() does the
     same */
  overview.Resize({RasterTraits::ToOverviewCeil(size.x),
                   RasterTraits::ToOverviewCeil(size.y)});
  overview_medium.Resize({
      RasterTraits::ToOverviewCeil(size.x,
                                   RasterTraits::OVERVIEW_MEDIUM_BITS),
      RasterTraits::ToOverviewCeil(size.y,
                                   RasterTraits::OVERVIEW_MEDIUM_BITS),
    });

  /* Invalid so tile contributions can max-merge into empty cells. */
  std::fill_n(overview.GetData(), overview.GetSize().Area(),
              TerrainHeight::Invalid());
  std::fill_n(overview_medium.GetData(), overview_medium.GetSize().Area(),
              TerrainHeight::Invalid());

  overview_size_fine = size << RasterTraits::SUBPIXEL_BITS;

  tiles.GrowDiscard(_n_tiles.x, _n_tiles.y);
}

void
RasterTileCache::SetLatLonBounds(double _lon_min, double _lon_max,
                                 double _lat_min, double _lat_max) noexcept
{
  const Angle lon_min(Angle::Degrees(_lon_min));
  const Angle lon_max(Angle::Degrees(_lon_max));
  const Angle lat_min(Angle::Degrees(_lat_min));
  const Angle lat_max(Angle::Degrees(_lat_max));

  bounds = GeoBounds(GeoPoint(std::min(lon_min, lon_max),
                              std::max(lat_min, lat_max)),
                     GeoPoint(std::max(lon_min, lon_max),
                              std::min(lat_min, lat_max)));
}

void
RasterTileCache::Reset() noexcept
{
  size = {0, 0};
  bounds.SetInvalid();
  segments.clear();

  overview.Reset();
  overview_medium.Reset();
  display_lod = DemOverview::Lod::FINE;

  for (auto &i : tiles)
    i.Unload();
}

void
RasterTileCache::UnloadTiles() noexcept
{
  bool any = false;
  for (auto &i : tiles) {
    if (!i.IsLoaded())
      continue;
    i.Unload();
    any = true;
  }

  if (!any)
    return;

  dirty = false;
  ++serial;
}

void
RasterTileCache::CopyLayoutFrom(const RasterTileCache &src) noexcept
{
  assert(src.IsValid());
  assert(&src != this);

  Reset();

  SetSize(src.size, src.tile_size,
          {src.tiles.GetWidth(), src.tiles.GetHeight()});
  bounds = src.bounds;

  segments.clear();
  for (const auto &s : src.segments)
    segments.append() = s;

  for (unsigned i = 0; i < tiles.GetSize(); ++i) {
    auto &dest = tiles.GetLinear(i);
    const auto &from = src.tiles.GetLinear(i);
    dest.Unload();
    dest.ClearRequest();
    if (from.IsDefined())
      dest.Set(from.start, from.end);
    else
      dest.Clear();
  }

  const auto overview_size = overview.GetSize();
  assert(overview_size.x == src.overview.GetSize().x);
  assert(overview_size.y == src.overview.GetSize().y);
  std::copy_n(src.overview.GetData(), overview_size.Area(),
              overview.GetData());

  const auto medium_size = overview_medium.GetSize();
  assert(medium_size.x == src.overview_medium.GetSize().x);
  assert(medium_size.y == src.overview_medium.GetSize().y);
  std::copy_n(src.overview_medium.GetData(), medium_size.Area(),
              overview_medium.GetData());

  display_lod = DemOverview::Lod::FINE;

  dirty = false;
  ++serial;
}

const RasterTileCache::MarkerSegmentInfo *
RasterTileCache::FindMarkerSegment(uint32_t file_offset) const noexcept
{
  for (const auto &s : segments)
    if (s.file_offset >= file_offset)
      return &s;

  return nullptr;
}

void
RasterTileCache::FinishTileUpdate() noexcept
{
  /* permanently disable the requested tiles which are still not
     loaded, to prevent trying to reload them over and over in a busy
     loop */
  for (std::size_t i : request_tiles) {
    RasterTile &tile = tiles.GetLinear(i);
    if (tile.IsRequested() && !tile.IsLoaded())
      tile.Clear();
  }

  ++serial;
}

void
RasterTileCache::SaveCache(BufferedOutputStream &os) const
{
  if (!IsValid())
    throw std::runtime_error("Terrain invalid");

  assert(bounds.IsValid());

  /* save metadata */
  CacheHeader header;

  /* zero-fill all implicit padding bytes (to make valgrind happy) */
  memset(&header, 0, sizeof(header));

  header.version = CacheHeader::VERSION;
  header.size = size;
  header.tile_size = tile_size;
  header.n_tiles = {tiles.GetWidth(), tiles.GetHeight()};
  header.num_marker_segments = segments.size();
  header.bounds = bounds;

  os.Write(ReferenceAsBytes(header));
  os.Write(std::as_bytes(std::span{segments}));

  /* save tiles */
  unsigned i;
  for (i = 0; i < tiles.GetSize(); ++i) {
    const auto &tile = tiles.GetLinear(i);
    if (tile.IsDefined()) {
      os.Write(ReferenceAsBytes(i));
      tile.SaveCache(os);
    }
  }

  i = -1;
  os.Write(ReferenceAsBytes(i));

  /* save overview (coarse then medium) */
  size_t overview_size = overview.GetSize().Area();
  os.Write(std::as_bytes(std::span{overview.GetData(), overview_size}));

  size_t medium_size = overview_medium.GetSize().Area();
  os.Write(std::as_bytes(std::span{overview_medium.GetData(), medium_size}));
}

void
RasterTileCache::LoadCache(BufferedReader &r)
{
  Reset();

  /* load metadata */
  const auto header = r.ReadFullT<CacheHeader>();

  if (header.version != CacheHeader::VERSION ||
      header.size.x < 1024 || header.size.x > 1024 * 1024 ||
      header.size.y < 1024 || header.size.y > 1024 * 1024 ||
      header.tile_size.x < 16 || header.tile_size.x > 16 * 1024 ||
      header.tile_size.y < 16 || header.tile_size.y > 16 * 1024 ||
      header.n_tiles.x < 1 || header.n_tiles.x > 1024 ||
      header.n_tiles.y < 1 || header.n_tiles.y > 1024 ||
      header.num_marker_segments < 4 ||
      header.num_marker_segments > segments.capacity() ||
      header.bounds.IsEmpty())
    throw std::runtime_error("Malformed terrain cache header");

  SetSize(header.size, header.tile_size, header.n_tiles);
  bounds = header.bounds;
  if (!bounds.IsValid())
    throw std::runtime_error("Malformed terrain cache bounds");

  /* load segments */
  for (unsigned i = 0; i < header.num_marker_segments; ++i) {
    segments.append() = r.ReadFullT<MarkerSegmentInfo>();
  }

  /* load tiles */
  while (true) {
    const auto i = r.ReadFullT<unsigned>();

    if (i == (unsigned)-1)
      break;

    if (i >= tiles.GetSize())
      throw std::runtime_error("Bad tile index");

    tiles.GetLinear(i).LoadCache(r);
  }

  /* load overview (coarse then medium) */
  size_t overview_size = overview.GetSize().Area();
  r.ReadFull(std::as_writable_bytes(std::span{
        overview.GetData(),
        overview_size,
      }));

  size_t medium_size = overview_medium.GetSize().Area();
  r.ReadFull(std::as_writable_bytes(std::span{
        overview_medium.GetData(),
        medium_size,
      }));
}
