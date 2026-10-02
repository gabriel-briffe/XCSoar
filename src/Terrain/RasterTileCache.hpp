// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "RasterTraits.hpp"
#include "DemOverview.hpp"
#include "RasterTile.hpp"
#include "RasterLocation.hpp"
#include "Geo/GeoBounds.hpp"
#include "util/StaticArray.hxx"
#include "util/Serial.hpp"

#include <cassert>
#include <cstdint>
#include <optional>

static constexpr unsigned  RASTER_SLOPE_FACT = 12;

struct jas_matrix;
struct GridLocation;
class BufferedOutputStream;
class BufferedReader;

class RasterTileCache {
  static constexpr unsigned MAX_RTC_TILES = 4096;

  /**
   * The maximum number of tiles which are loaded at a time.  This
   * must be limited because the amount of memory is finite.
   */
#if defined(ANDROID)
  static constexpr unsigned MAX_ACTIVE_TILES = 128;
#else
  // desktop: use a lot of memory
  static constexpr unsigned MAX_ACTIVE_TILES = 512;
#endif

  /**
   * Target number of steps in intersection searches; total distance
   * is shifted by this number of bits
   */
  static constexpr unsigned INTERSECT_BITS = 7;

protected:
  friend struct RTDistanceSort;
  friend class TerrainLoader;

  struct MarkerSegmentInfo {
    static constexpr uint16_t NO_TILE = (uint16_t)-1;

    /**
     * The position of this marker segment within the file.
     */
    uint32_t file_offset;

    /**
     * The associated tile number.  -1 if this segment does not belong
     * to a tile.
     */
    uint16_t tile;

    /**
     * The number of follow-up segments.
     */
    uint16_t count;

    MarkerSegmentInfo() noexcept = default;

    constexpr MarkerSegmentInfo(uint32_t _file_offset,
                                int _tile=NO_TILE) noexcept
      :file_offset(_file_offset), tile(_tile), count(0) {}

    constexpr bool IsTileSegment() const noexcept {
      return tile != NO_TILE;
    }
  };

  struct CacheHeader {
    /** 0xd: global-aligned max-pool with cross-tile merge. */
    static constexpr unsigned VERSION = 0xd;

    unsigned version;
    UnsignedPoint2D size;
    Point2D<uint_least16_t> tile_size;
    UnsignedPoint2D n_tiles;
    unsigned num_marker_segments;
    GeoBounds bounds;
  };

  bool dirty;

  /**
   * This serial gets updated each time the tiles get loaded or
   * discarded.
   */
  Serial serial;

  AllocatedGrid<RasterTile> tiles;
  Point2D<uint_least16_t> tile_size;

  /** Coarse full-map height buffer (1/16 fine, max-pooled). */
  RasterBuffer overview;
  /** Medium full-map height buffer (1/4 fine, max-pooled). */
  RasterBuffer overview_medium;
  RasterLocation size;
  RasterLocation overview_size_fine;

  /**
   * Map-display LOD chosen from cells-per-pixel.  Height queries and
   * the glide-cone path ignore this and keep using fine tiles +
   * coarse overview.
   */
  DemOverview::Lod display_lod = DemOverview::Lod::FINE;

  GeoBounds bounds;

  StaticArray<MarkerSegmentInfo, 8192> segments;

  /**
   * An array that is used to sort the requested tiles by distance.
   * This is only used by PollTiles() internally, but is stored in the
   * class because it would be too large for the stack.
   */
  StaticArray<uint16_t, MAX_RTC_TILES> request_tiles;

public:
  RasterTileCache() noexcept {
    Reset();
  }

  RasterTileCache(const RasterTileCache &) = delete;
  RasterTileCache &operator=(const RasterTileCache &) = delete;

  void SetBounds(const GeoBounds &_bounds) noexcept {
    assert(_bounds.IsValid());

    bounds = _bounds;
  }

protected:
  void ScanTileLine(GridLocation start, GridLocation end,
                    TerrainHeight *buffer, unsigned size,
                    bool interpolate) const noexcept;

public:
  /**
   * Determine the non-interpolated height at the specified pixel
   * location.
   *
   * @param x the pixel position within the map; may be out of range
   */
  [[gnu::pure]]
  TerrainHeight GetHeight(RasterLocation p) const noexcept;

  /**
   * Determine the interpolated height at the specified sub-pixel
   * location.
   *
   * @param p the sub-pixel position within the map; may be out of range
   */
  [[gnu::pure]]
  TerrainHeight GetInterpolatedHeight(RasterLocation p) const noexcept;

  /**
   * Scan a straight line and fill the buffer with the specified
   * number of samples along the line.
   *
   * @param start the sub-pixel start location
   * @param end the sub-pixel end location
   */
  void ScanLine(const RasterLocation start, const RasterLocation end,
                TerrainHeight *buffer, unsigned size,
                bool interpolate) const noexcept;

  struct Intersection {
    RasterLocation location;
    int height;
  };

  [[gnu::pure]]
  std::optional<Intersection> FirstIntersection(SignedRasterLocation origin,
                                                SignedRasterLocation destination,
                                                int h_origin,
                                                int h_dest,
                                                int slope_fact, int h_ceiling,
                                                int h_safety,
                                                bool can_climb) const noexcept;

  /**
   * @return {-1,-1} if no intersection was found
   */
  [[gnu::pure]] SignedRasterLocation
  GroundIntersection(SignedRasterLocation origin,
                     SignedRasterLocation destination,
                     int h_origin, const int slope_fact,
                     int height_floor) const noexcept;

private:
  /**
   * Get field (not interpolated) directly, without bringing tiles to front.
   * @param p position/256
   * @param tile_index Remember position of active tile, or -1 for overview
   * @return the terrain altitude and a flag that is true when the
   * value was loaded from a "fine" tile
   */
  [[gnu::pure]]
  std::pair<TerrainHeight, bool> GetFieldDirect(RasterLocation p) const noexcept;

public:
  /**
   * Throws on error.
   */
  void SaveCache(BufferedOutputStream &os) const;

  /**
   * Throws on error.
   */
  void LoadCache(BufferedReader &r);

  /**
   * Determines if there are still tiles scheduled to be loaded.  Call
   * this after UpdateTiles() to determine if UpdateTiles() should be
   * called again soon.
   */
  bool IsDirty() const noexcept {
    return dirty;
  }

  bool IsValid() const noexcept {
    return bounds.IsValid();
  }

  const Serial &GetSerial() const noexcept {
    return serial;
  }

  void Reset() noexcept;

  /**
   * Drop all loaded fine tiles; keep overview / metadata so the next
   * UpdateTiles() can reload.  Used for ephemeral compute DEM windows.
   */
  void UnloadTiles() noexcept;

  /**
   * Clone layout metadata (bounds, tile table, file segments, overview
   * buffer) from @p src into this cache.  Fine tiles are not shared —
   * dest starts with no HD tiles loaded.  Used so GlideCone can avoid a
   * second overview scan of the map file.
   */
  void CopyLayoutFrom(const RasterTileCache &src) noexcept;

  const GeoBounds &GetBounds() const noexcept {
    assert(bounds.IsValid());

    return bounds;
  }

public:
  /* methods called by class TerrainLoader */

  [[gnu::pure]]
  const MarkerSegmentInfo *
  FindMarkerSegment(uint32_t file_offset) const noexcept;

  long SkipMarkerSegment(long file_offset) const noexcept;
  void MarkerSegment(long file_offset, unsigned id) noexcept;

  void StartTile(unsigned index) noexcept {
    if (!segments.empty() && !segments.back().IsTileSegment())
      /* link current marker segment with this tile */
      segments.back().tile = index;
  }

  void SetSize(UnsignedPoint2D size,
               Point2D<uint_least16_t> tile_size,
               UnsignedPoint2D n_tiles) noexcept;

  void SetLatLonBounds(double lon_min, double lon_max,
                       double lat_min, double lat_max) noexcept;

  void PutOverviewTile(unsigned index,
                       RasterLocation start, RasterLocation end,
                       const struct jas_matrix &m) noexcept;

  /**
   * Update which tiles cover @p radius around @p p.
   * @param load_fine when true, request JP2 loads (HD).  When false,
   *   only refresh the active set for overview draw and unload fine
   *   buffers — no JP2 decode.
   * @return true if fine tiles still need loading
   */
  bool PollTiles(SignedRasterLocation p, unsigned radius,
                 bool load_fine = true) noexcept;

  /**
   * Tile indices covering the view after the last #PollTiles call.
   */
  const auto &GetActiveTiles() const noexcept {
    return request_tiles;
  }

  /**
   * How many defined JP2 (DEM1) tiles intersect @p radius around @p p
   * (same +256 margin as #PollTiles).
   */
  [[gnu::pure]]
  unsigned CountTilesInView(SignedRasterLocation p,
                            unsigned radius) const noexcept;

  /**
   * JP2 tiles covered by one "slot" at @p lod (1, 16, or 256).
   * DEM2/DEM3 budgets are expressed as #MAX_ACTIVE_TILES of these
   * larger tiles; this converts to an equivalent JP2 count.
   */
  [[gnu::const]]
  static constexpr unsigned
  Jp2TilesPerLodTile(DemOverview::Lod lod) noexcept {
    switch (lod) {
    case DemOverview::Lod::FINE:
      return 1;
    case DemOverview::Lod::MEDIUM:
      return 1u << (2u * RasterTraits::OVERVIEW_MEDIUM_BITS);
    case DemOverview::Lod::COARSE:
      return 1u << (2u * RasterTraits::OVERVIEW_BITS);
    }
    return 1;
  }

  /**
   * How many tiles at @p lod fit the view (ceil of JP2 count /
   * #Jp2TilesPerLodTile).  Auto compares this to #MAX_ACTIVE_TILES.
   */
  [[gnu::pure]]
  unsigned CountLodTilesInView(SignedRasterLocation p, unsigned radius,
                               DemOverview::Lod lod) const noexcept;

  /**
   * True when #CountLodTilesInView exceeds #MAX_ACTIVE_TILES for @p lod
   * (same rule at every DEM level).
   */
  [[gnu::pure]]
  bool ExceedsActiveTileBudget(SignedRasterLocation p,
                               unsigned radius,
                               DemOverview::Lod lod =
                                 DemOverview::Lod::FINE) const noexcept;

  /**
   * Max JP2 tiles #PollTiles may keep active for @p lod
   * (#MAX_ACTIVE_TILES × #Jp2TilesPerLodTile).
   */
  [[gnu::const]]
  static constexpr unsigned
  MaxActiveTilesForLod(DemOverview::Lod lod) noexcept {
    const unsigned n = MAX_ACTIVE_TILES * Jp2TilesPerLodTile(lod);
    return n < MAX_RTC_TILES ? n : MAX_RTC_TILES;
  }

  /**
   * Auto LOD: DEM1 while ≤ #MAX_ACTIVE_TILES DEM1 tiles cover the
   * view; else DEM2 while ≤ #MAX_ACTIVE_TILES DEM2 tiles; else DEM3.
   */
  [[gnu::pure]]
  DemOverview::Lod
  SelectLodByTileBudget(SignedRasterLocation p,
                        unsigned radius) const noexcept;

  void PutTileData(unsigned index, const struct jas_matrix &m) noexcept;

  void FinishTileUpdate() noexcept;

public:
  TerrainHeight GetMaxElevation() const noexcept {
    return overview.GetMaximum();
  }

  /**
   * Coarse full-map height buffer (1/16 of fine DEM resolution).
   */
  const RasterBuffer &GetOverview() const noexcept {
    return overview;
  }

  /**
   * Medium full-map height buffer (1/4 of fine DEM resolution).
   */
  const RasterBuffer &GetOverviewMedium() const noexcept {
    return overview_medium;
  }

  DemOverview::Lod GetDisplayLod() const noexcept {
    return display_lod;
  }

  /**
   * Set map-display LOD.  Bumps #serial when the value changes so
   * renderers rebuild.
   */
  void SetDisplayLod(DemOverview::Lod lod) noexcept {
    if (display_lod == lod)
      return;
    display_lod = lod;
    ++serial;
  }

  Point2D<uint_least16_t> GetTileSize() const noexcept {
    return tile_size;
  }

  unsigned GetTileCountX() const noexcept {
    return tiles.GetWidth();
  }

  unsigned GetTileCountY() const noexcept {
    return tiles.GetHeight();
  }

  const RasterTile &GetTile(unsigned x, unsigned y) const noexcept {
    return tiles.Get(x, y);
  }

  /**
   * Is the given point inside the map?
   */
  bool IsInside(RasterLocation p) const noexcept {
    return p.x < size.x && p.y < size.y;
  }

  const auto &GetSize() const noexcept {
    return size;
  }

  RasterLocation GetFineSize() const noexcept {
    return size << RasterTraits::SUBPIXEL_BITS;
  }

private:
  RasterLocation GetFineTileSize() const noexcept {
    return {
      unsigned(tile_size.x) << RasterTraits::SUBPIXEL_BITS,
      unsigned(tile_size.y) << RasterTraits::SUBPIXEL_BITS,
    };
  }
};
