// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"

#include <cstdint>
#include <optional>
#include <vector>

struct GlideConeField;
struct GlideConeSettings;
struct MapLook;
class Canvas;
class WindowProjection;

/**
 * Optional-area mask: cells reachable downhill from the aircraft while
 * staying above ground clearance and the airport cone.  Arrival uses a
 * straight line to the last visible origin, matching the GPU shader.
 */
namespace GlideConeOptions {

void RequestOnce() noexcept;
void Clear() noexcept;

[[gnu::pure]]
std::optional<unsigned> LastComputeMs() noexcept;

/**
 * Descending arrival altitude [m MSL] at @p location when that cell is
 * in the optional area; nullopt if options are inactive or the cell is
 * outside the green patch.
 */
[[gnu::pure]]
std::optional<double> QueryArrivalAltitude(GeoPoint location) noexcept;

void Update(const GlideConeField &field, double start_alt,
            GeoPoint start_location, int gi, int gj,
            const GlideConeSettings &settings) noexcept;

void Draw(Canvas &canvas, const WindowProjection &projection,
          const GlideConeSettings &settings) noexcept;

/**
 * Aircraft → airport with the highest options arrival (green).
 *
 * @return true if drawn (omit the pink glide-cone path); false if the
 *         caller should draw the pink cone path instead.
 */
bool DrawBestAirportPath(Canvas &canvas, const WindowProjection &projection,
                         const MapLook &look) noexcept;

/**
 * Pan mode only: pink aircraft → option cell under the probe, then
 * (after settle debounce) a fresh downward from that cell and a green
 * path to the highest-arrival airport.
 */
bool DrawPanPaths(Canvas &canvas, const WindowProjection &projection,
                  GeoPoint pan_probe, const GlideConeField &field,
                  const MapLook &look) noexcept;

} // namespace GlideConeOptions
