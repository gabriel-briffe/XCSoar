// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Engine/Waypoint/Waypoint.hpp"

#include <cstddef>

/**
 * Waypoint types exposed in the map filter dialog and per-page filter
 * catalog.  SeeYou / OpenAIP CUP styles — thermal hotspot is not a CUP
 * style and is omitted here.
 */
static constexpr Waypoint::Type waypoint_map_filter_types[] = {
  Waypoint::Type::NORMAL,
  Waypoint::Type::AIRFIELD,
  Waypoint::Type::OUTLANDING,
  Waypoint::Type::MOUNTAIN_PASS,
  Waypoint::Type::MOUNTAIN_TOP,
  Waypoint::Type::OBSTACLE,
  Waypoint::Type::VOR,
  Waypoint::Type::NDB,
  Waypoint::Type::TOWER,
  Waypoint::Type::DAM,
  Waypoint::Type::TUNNEL,
  Waypoint::Type::BRIDGE,
  Waypoint::Type::POWERPLANT,
  Waypoint::Type::CASTLE,
  Waypoint::Type::INTERSECTION,
  Waypoint::Type::MARKER,
  Waypoint::Type::REPORTING_POINT,
  Waypoint::Type::PGTAKEOFF,
  Waypoint::Type::PGLANDING,
};

static constexpr unsigned WAYPOINT_MAP_FILTER_TYPE_COUNT =
  std::size(waypoint_map_filter_types);

static constexpr unsigned WAYPOINT_TYPE_COUNT =
  unsigned(Waypoint::Type::PGLANDING) + 1;

[[nodiscard]]
const char *
GetWaypointMapFilterTypeName(Waypoint::Type type) noexcept;
