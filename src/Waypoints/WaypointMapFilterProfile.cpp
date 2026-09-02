// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Waypoints/WaypointMapFilterProfile.hpp"

#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
#include "util/StringFormat.hpp"

#include <cassert>

namespace WaypointMapFilterProfile {

void
FormatTypeDisplayKey(char *buffer, size_t size,
                     unsigned type_index) noexcept
{
  const int written =
    StringFormat(buffer, size, "WaypointTypeDisplay%u", type_index);
  assert(written > 0 && size_t(written) < size);
  if (written <= 0 || size_t(written) >= size)
    buffer[0] = '\0';
}

bool
LoadTypeDisplay(unsigned type_index) noexcept
{
  if (type_index >= WAYPOINT_TYPE_COUNT)
    return true;

  char name[64];
  FormatTypeDisplayKey(name, sizeof(name), type_index);
  bool display = true;
  Profile::Get(name, display);
  return display;
}

void
SaveTypeDisplay(unsigned type_index, bool display) noexcept
{
  if (type_index >= WAYPOINT_TYPE_COUNT)
    return;

  char name[64];
  FormatTypeDisplayKey(name, sizeof(name), type_index);
  Profile::Set(name, display);
}

bool
LoadNonIcaoAirports() noexcept
{
  bool display = true;
  Profile::Get(ProfileKeys::WaypointDisplayNonIcaoAirports, display);
  return display;
}

void
SaveNonIcaoAirports(bool display) noexcept
{
  Profile::Set(ProfileKeys::WaypointDisplayNonIcaoAirports, display);
}

void
Load(WaypointRendererSettings &settings) noexcept
{
  for (unsigned i = 0; i < WAYPOINT_TYPE_COUNT; ++i)
    settings.display_types[i] = LoadTypeDisplay(i);

  settings.display_non_icao_airports = LoadNonIcaoAirports();
}

} // namespace WaypointMapFilterProfile
