// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSettingModule.hpp"

#include "Elements/ElementsDisplaySetting.hpp"
#include "Language/Language.hpp"
#include "Orientation/OrientationDisplaySetting.hpp"
#include "Terrain/TerrainDisplaySetting.hpp"
#include "Waypoints/WaypointsDisplaySetting.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "util/Macros.hpp"

#include <cassert>

namespace PageSettingModuleRegistry {

namespace {

static const PageSettingModule modules[] = {
  {
    PageSettingGroup::TERRAIN,
    PageSettingId::TERRAIN_ENABLE,
    N_("Terrain"),
    TerrainDisplaySetting::Count,
    TerrainDisplaySetting::Get,
    TerrainDisplaySetting::IsValidValue,
    TerrainDisplaySetting::GetLive,
    TerrainDisplaySetting::SetLive,
    TerrainDisplaySetting::LoadGlobal,
    TerrainDisplaySetting::SaveGlobal,
  },
  {
    PageSettingGroup::ORIENTATION,
    PageSettingId::CRUISE_ORIENTATION,
    N_("Orientation"),
    OrientationDisplaySetting::Count,
    OrientationDisplaySetting::Get,
    OrientationDisplaySetting::IsValidValue,
    OrientationDisplaySetting::GetLive,
    OrientationDisplaySetting::SetLive,
    OrientationDisplaySetting::LoadGlobal,
    OrientationDisplaySetting::SaveGlobal,
  },
  {
    PageSettingGroup::ELEMENTS,
    PageSettingId::GROUND_TRACK,
    N_("Elements"),
    ElementsDisplaySetting::Count,
    ElementsDisplaySetting::Get,
    ElementsDisplaySetting::IsValidValue,
    ElementsDisplaySetting::GetLive,
    ElementsDisplaySetting::SetLive,
    ElementsDisplaySetting::LoadGlobal,
    ElementsDisplaySetting::SaveGlobal,
  },
  {
    PageSettingGroup::WAYPOINTS,
    PageSettingId::WAYPOINT_LABEL_FORMAT,
    N_("Waypoints"),
    WaypointsDisplaySetting::Count,
    WaypointsDisplaySetting::Get,
    WaypointsDisplaySetting::IsValidValue,
    WaypointsDisplaySetting::GetLive,
    WaypointsDisplaySetting::SetLive,
    WaypointsDisplaySetting::LoadGlobal,
    WaypointsDisplaySetting::SaveGlobal,
  },
  {
    PageSettingGroup::AIRSPACE,
    PageSettingId::AIRSPACE_DISPLAY,
    N_("Airspace"),
    AirspaceDisplaySetting::Count,
    AirspaceDisplaySetting::Get,
    AirspaceDisplaySetting::IsValidValue,
    AirspaceDisplaySetting::GetLive,
    AirspaceDisplaySetting::SetLive,
    AirspaceDisplaySetting::LoadGlobal,
    AirspaceDisplaySetting::SaveGlobal,
  },
};

static_assert(ARRAY_SIZE(modules) == unsigned(PageSettingGroup::COUNT),
              "Module table must match PageSettingGroup::COUNT");

} // namespace

unsigned
Count() noexcept
{
  return unsigned(PageSettingGroup::COUNT);
}

const PageSettingModule &
Get(unsigned index) noexcept
{
  assert(index < unsigned(PageSettingGroup::COUNT));
  return modules[index];
}

const PageSettingModule &
Get(PageSettingGroup group) noexcept
{
  assert(unsigned(group) < unsigned(PageSettingGroup::COUNT));
  return modules[unsigned(group)];
}

const PageSettingModule &
GetById(PageSettingId id) noexcept
{
  for (unsigned i = 0; i < Count(); ++i) {
    const auto &module = modules[i];
    const unsigned u = unsigned(id);
    if (u >= unsigned(module.id_start) &&
        u < unsigned(module.id_start) + module.count())
      return module;
  }

  assert(false);
  return modules[0];
}

const char *
GetLabel(PageSettingGroup group) noexcept
{
  return Get(group).label;
}

} // namespace PageSettingModuleRegistry
