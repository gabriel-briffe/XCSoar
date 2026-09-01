// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSettingModule.hpp"

#include "Language/Language.hpp"
#include "MapDisplaySetting.hpp"
#include "Terrain/TerrainDisplaySetting.hpp"
#include "util/Macros.hpp"

#include <cassert>

namespace PageSettingModuleRegistry {

namespace {

static const PageSettingModule modules[] = {
  {
    PageSettingGroup::TERRAIN,
    N_("Terrain"),
    TerrainDisplaySetting::Count,
    TerrainDisplaySetting::Get,
    TerrainDisplaySetting::Get,
    TerrainDisplaySetting::IsValidValue,
    TerrainDisplaySetting::GetLive,
    TerrainDisplaySetting::SetLive,
    TerrainDisplaySetting::LoadGlobal,
    TerrainDisplaySetting::SaveGlobal,
  },
  {
    PageSettingGroup::MAP,
    N_("Map display"),
    MapDisplaySetting::Count,
    MapDisplaySetting::Get,
    MapDisplaySetting::Get,
    MapDisplaySetting::IsValidValue,
    MapDisplaySetting::GetLive,
    MapDisplaySetting::SetLive,
    MapDisplaySetting::LoadGlobal,
    MapDisplaySetting::SaveGlobal,
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
  return Get(PageSettingGroupForId(id));
}

const char *
GetLabel(PageSettingGroup group) noexcept
{
  return Get(group).label;
}

} // namespace PageSettingModuleRegistry
