// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettingDescriptor.hpp"
#include "MapSettings.hpp"

/**
 * Map Display → Elements: shared catalog and get/set for global profile
 * and live MapSettings.
 */
namespace ElementsDisplaySetting {

struct Bundle {
  DisplayGroundTrack display_ground_track = DisplayGroundTrack::AUTO;
  bool show_flarm_on_map = true;
  bool fade_traffic = true;
  TrailSettings trail;
  bool detour_cost_markers_enabled = false;
  AircraftSymbol aircraft_symbol = AircraftSymbol::SIMPLE;
  WindArrowStyle wind_arrow_style = WindArrowStyle::ARROW_HEAD;
  DisplayOnlineTrafficMapMode online_traffic_map_mode =
    DisplayOnlineTrafficMapMode::SYMBOL;
  bool distance_rings_enabled = false;
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

} // namespace ElementsDisplaySetting
