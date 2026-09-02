// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Waypoints/WaypointMapFilterTypes.hpp"

#include "Language/Language.hpp"

#include <cassert>

const char *
GetWaypointMapFilterTypeName(Waypoint::Type type) noexcept
{
  switch (type) {
  case Waypoint::Type::NORMAL:
    return _("Turnpoint");
  case Waypoint::Type::AIRFIELD:
    return _("Airport");
  case Waypoint::Type::OUTLANDING:
    return _("Landable");
  case Waypoint::Type::MOUNTAIN_PASS:
    return _("Mountain Pass");
  case Waypoint::Type::MOUNTAIN_TOP:
    return _("Mountain Top");
  case Waypoint::Type::OBSTACLE:
    return _("Transmitter Mast");
  case Waypoint::Type::TOWER:
    return _("Tower");
  case Waypoint::Type::TUNNEL:
    return _("Tunnel");
  case Waypoint::Type::BRIDGE:
    return _("Bridge");
  case Waypoint::Type::POWERPLANT:
    return _("Power Plant");
  case Waypoint::Type::VOR:
    return _("VOR");
  case Waypoint::Type::NDB:
    return _("NDB");
  case Waypoint::Type::DAM:
    return _("Dam");
  case Waypoint::Type::CASTLE:
    return _("Castle");
  case Waypoint::Type::INTERSECTION:
    return _("Intersection");
  case Waypoint::Type::MARKER:
    return _("Marker");
  case Waypoint::Type::REPORTING_POINT:
    return _("Control Point");
  case Waypoint::Type::PGTAKEOFF:
    return _("PG Take Off");
  case Waypoint::Type::PGLANDING:
    return _("PG Landing Zone");
  case Waypoint::Type::THERMAL_HOTSPOT:
    break;
  }

  assert(false);
  return _("Unknown");
}
