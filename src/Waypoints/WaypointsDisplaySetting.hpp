// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettingDescriptor.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
#include "Waypoints/WaypointMapFilterTypes.hpp"

/**
 * Map Display → Waypoints: shared catalog and get/set for global profile
 * and live MapSettings.  Per-type map filters occupy
 * [#WAYPOINT_TYPE_FILTER_BEGIN, #WAYPOINT_DISPLAY_NON_ICAO_AIRPORTS).
 */
namespace WaypointsDisplaySetting {

struct Bundle {
  WaypointRendererSettings waypoint;
};

[[nodiscard]]
unsigned
Count() noexcept;

[[nodiscard]]
const PageSettingDescriptor &
Get(PageSettingId id) noexcept;

[[nodiscard]]
const PageSettingDescriptor &
Get(unsigned index) noexcept;

[[nodiscard]]
bool
IsValidValue(PageSettingId id, int value) noexcept;

[[nodiscard]]
int
GetLive(PageSettingId id) noexcept;

void
SetLive(PageSettingId id, int value) noexcept;

[[nodiscard]]
int
LoadGlobal(PageSettingId id) noexcept;

void
SaveGlobal(PageSettingId id, int value) noexcept;

void
ReadLive(Bundle &bundle) noexcept;

void
ApplyLive(const Bundle &bundle) noexcept;

[[nodiscard]]
int
GetValue(const Bundle &bundle, PageSettingId id) noexcept;

void
SetValue(Bundle &bundle, PageSettingId id, int value) noexcept;

void
LoadGlobal(Bundle &bundle) noexcept;

/**
 * Persist global profile keys that differ from @p initial.
 * @return true when the profile was modified
 */
bool
SaveGlobal(const Bundle &current, const Bundle &initial) noexcept;

[[nodiscard]]
constexpr bool
IsTypeFilter(PageSettingId id) noexcept
{
  return unsigned(id) >= unsigned(PageSettingId::WAYPOINT_TYPE_FILTER_BEGIN) &&
         unsigned(id) < unsigned(PageSettingId::WAYPOINT_DISPLAY_NON_ICAO_AIRPORTS);
}

[[nodiscard]]
constexpr bool
IsNonIcaoFilter(PageSettingId id) noexcept
{
  return id == PageSettingId::WAYPOINT_DISPLAY_NON_ICAO_AIRPORTS;
}

[[nodiscard]]
constexpr Waypoint::Type
TypeFromFilterId(PageSettingId id) noexcept
{
  return waypoint_map_filter_types[unsigned(id) -
                                   unsigned(PageSettingId::WAYPOINT_TYPE_FILTER_BEGIN)];
}

[[nodiscard]]
constexpr unsigned
FilterDialogRowCount() noexcept
{
  return PageSettingWaypointTypeFilterCount + 1;
}

[[nodiscard]]
PageSettingId
FilterDialogRowId(unsigned row) noexcept;

} // namespace WaypointsDisplaySetting
