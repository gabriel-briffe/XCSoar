// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "XCThermGeoJSON.hpp"

#include <vector>

namespace XCThermGeoJSON {

/** Counters for one import cleanup pass (optional logging). */
struct CleanupStats {
  unsigned dedupe = 0;         /**< consecutive/closing duplicate verts */
  unsigned self_crossings = 0; /**< rings split at an edge crossing */
  unsigned junk = 0;           /**< rings dropped (degenerate / bailout) */
};

/**
 * Post-import cleanup for one ring (exterior or hole):
 * - drop consecutive duplicate vertices
 * - drop junk (fewer than 3 vertices, near-zero area)
 * - split self-crossing rings into simple rings
 *
 * Concave rings are kept intact; OpenGL earcuts at draw time.
 * Returns zero or more single-ring polygons.
 * When @p stats is non-null, increments cleanup counters.
 */
std::vector<std::vector<Ring>>
CleanExterior(const Ring &exterior,
              CleanupStats *stats = nullptr) noexcept;

/**
 * Clean every polygon in @p band.  Keeps holes when the exterior stays
 * as one simple ring; if the exterior must be split, holes are dropped
 * for those parts.  Empty results are removed.
 * When @p stats is non-null, increments cleanup counters.
 */
void
CleanBandPolygons(WindBand &band,
                  CleanupStats *stats = nullptr) noexcept;

/**
 * Sort bands by ascending |midpoint| so weaker fills draw first and
 * stronger bands overwrite (matches opaque overlay compositing).
 */
void
SortBandsByAbsMid(ForecastLayer &layer) noexcept;

} // namespace XCThermGeoJSON
