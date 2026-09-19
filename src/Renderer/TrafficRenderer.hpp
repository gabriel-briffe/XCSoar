// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "FLARM/Color.hpp"
#include "FLARM/Traffic.hpp"

#include <cstdint>

struct PixelPoint;
class Canvas;
class Color;
class Pen;
struct TrafficLook;
struct GliderLinkTraffic;
class Angle;
enum class TrafficSymbol : uint8_t;
enum class AircraftTypeSymbolStyle : uint8_t;

namespace TrafficRenderer
{
/**
 * Draw a FLARM traffic symbol on the map.
 *
 * @param symbol the symbol style from #MapSettings::traffic_symbol
 * @param symbol_style halo/glyph composition for aircraft-type symbols
 */
void
Draw(Canvas &canvas, const TrafficLook &traffic_look,
     TrafficSymbol symbol, AircraftTypeSymbolStyle symbol_style,
     bool fading,
     const FlarmTraffic &traffic, Angle angle,
     FlarmColor color, PixelPoint pt) noexcept;

/**
 * Draw a traffic symbol scaled to fit a list row icon slot.
 */
void
DrawList(Canvas &canvas, const TrafficLook &traffic_look,
         TrafficSymbol symbol, AircraftTypeSymbolStyle symbol_style,
         const FlarmTraffic &traffic, Angle angle,
         FlarmColor color, PixelPoint pt,
         unsigned icon_size) noexcept;

void
Draw(Canvas &canvas, const TrafficLook &traffic_look,
     TrafficSymbol symbol, AircraftTypeSymbolStyle symbol_style,
     const GliderLinkTraffic &traffic, Angle angle, PixelPoint pt) noexcept;

/**
 * Is there an aircraft-type symbol for this type?  A type without one
 * is drawn as the classic arrow head.
 */
[[gnu::const]]
bool
HasAircraftTypeSymbol(FlarmTraffic::AircraftType type) noexcept;

/**
 * Draw the aircraft-type symbol for the given style.
 * Works on all canvas backends because the symbol is drawn as rotated
 * polygons.
 *
 * @param size the height of the (square) symbol box in pixels; the
 * glyph itself is somewhat smaller
 * @param body_color the traffic colour (altitude / alarm)
 * @param glyph_color the contrast colour of the glyph (COLOURED_HALO
 * styles) or of its outline (BLACK_OUTLINE)
 * @param halo_color the halo colour, usually the background colour
 * (WHITE_HALO)
 * @param border_pen the pen for the outline of the halo, e.g. the
 * pen that would draw the classic arrow head (COLOURED_HALO_OUTLINED)
 */
void
DrawAircraftTypeSymbol(Canvas &canvas, FlarmTraffic::AircraftType type,
                       Angle angle, PixelPoint pt, unsigned size,
                       AircraftTypeSymbolStyle style,
                       Color body_color, Color glyph_color, Color halo_color,
                       const Pen &border_pen) noexcept;

/**
 * Pixel height of map traffic symbols (DPI-aware, not window size).
 */
[[gnu::const]]
unsigned MapIconSize() noexcept;

/**
 * Label offsets for map traffic symbols, derived from #MapIconSize().
 */
struct MapTrafficLabelLayout {
  unsigned icon_size;
  /** Subtract from symbol centre Y for the callsign anchor. */
  int name_offset_y;
  /** Add to symbol centre Y for the climb-rate anchor. */
  int climb_offset_y;
  /** Minimum own-ship distance (px) before labels are drawn. */
  int min_label_distance;
};

[[gnu::const]]
MapTrafficLabelLayout MapLabelLayout() noexcept;
}
