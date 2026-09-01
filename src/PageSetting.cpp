// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSetting.hpp"
#include "PageSettingDescriptor.hpp"
#include "ActionInterface.hpp"
#include "Interface.hpp"
#include "LogFile.hpp"
#include "PageSettings.hpp"
#include "Terrain/TerrainDisplaySetting.hpp"
#include "UISettings.hpp"

#include <cassert>

bool
PageSettingOverrides::Contains(PageSettingId id) const noexcept
{
  for (unsigned i = 0; i < n_items; ++i)
    if (items[i].id == id)
      return true;
  return false;
}

int *
PageSettingOverrides::FindValue(PageSettingId id) noexcept
{
  for (unsigned i = 0; i < n_items; ++i)
    if (items[i].id == id)
      return &items[i].value;
  return nullptr;
}

const int *
PageSettingOverrides::FindValue(PageSettingId id) const noexcept
{
  for (unsigned i = 0; i < n_items; ++i)
    if (items[i].id == id)
      return &items[i].value;
  return nullptr;
}

bool
PageSettingOverrides::Add(PageSettingId id, int value) noexcept
{
  if (Contains(id))
    return false;
  if (n_items >= MAX_ITEMS)
    return false;

  items[n_items++] = {id, value};
  return true;
}

bool
PageSettingOverrides::Remove(PageSettingId id) noexcept
{
  for (unsigned i = 0; i < n_items; ++i) {
    if (items[i].id != id)
      continue;

    for (unsigned j = i + 1; j < n_items; ++j)
      items[j - 1] = items[j];
    --n_items;
    return true;
  }
  return false;
}

void
PageSettingOverrides::SetValue(PageSettingId id, int value) noexcept
{
  if (int *v = FindValue(id); v != nullptr) {
    *v = value;
    return;
  }

  Add(id, value);
}

namespace PageSettingRegistry {

unsigned
Count() noexcept
{
  return TerrainDisplaySetting::Count();
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  return TerrainDisplaySetting::Get(id);
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  return TerrainDisplaySetting::Get(index);
}

bool
IsValidValue(const PageSettingDescriptor &desc, int value) noexcept
{
  return TerrainDisplaySetting::IsValidValue(desc.id, value);
}

} // namespace PageSettingRegistry

void
PageSettingNotifyLive() noexcept
{
  ActionInterface::SendMapSettings(true);
}

int
PageSettingGet(PageSettingId id) noexcept
{
  return TerrainDisplaySetting::LoadGlobal(id);
}

int
PageSettingGet(PageSettingId id, unsigned page_index) noexcept
{
  if (page_index < PageSettings::MAX_PAGES) {
    const auto &overrides =
      CommonInterface::GetUISettings().pages.overrides[page_index];
    if (const int *value = overrides.FindValue(id); value != nullptr &&
        *value != PageSettingOverrides::INHERIT)
      return *value;
  }

  return TerrainDisplaySetting::LoadGlobal(id);
}

void
PageSettingSet(PageSettingId id, int value) noexcept
{
  if (!TerrainDisplaySetting::IsValidValue(id, value)) {
    LogFmt("perPage: Set global reject id={} value={} (invalid)",
           unsigned(id), value);
    return;
  }

  if (value == PageSettingOverrides::INHERIT)
    value = TerrainDisplaySetting::LoadGlobal(id);

  const auto &desc = TerrainDisplaySetting::Get(id);
  LogFmt("perPage: Set global '{}' value={}", desc.label, value);
  TerrainDisplaySetting::SetLive(id, value);
  TerrainDisplaySetting::SaveGlobal(id, value);
  PageSettingNotifyLive();
}

void
PageSettingSet(PageSettingId id, int value, unsigned page_index) noexcept
{
  if (!TerrainDisplaySetting::IsValidValue(id, value)) {
    LogFmt("perPage: Set page reject index={} id={} value={} (invalid)",
           page_index, unsigned(id), value);
    return;
  }

  auto &pages = CommonInterface::SetUISettings().pages;
  if (page_index >= PageSettings::MAX_PAGES) {
    LogFmt("perPage: Set page reject index={} id={}",
           page_index, unsigned(id));
    return;
  }

  const auto &desc = TerrainDisplaySetting::Get(id);
  LogFmt("perPage: Set page={} '{}' value={}",
         page_index, desc.label, value);
  pages.overrides[page_index].SetValue(id, value);
}

void
PageSettingApply(PageSettingId id, int value,
                 std::optional<unsigned> page_index) noexcept
{
  if (!page_index.has_value())
    PageSettingSet(id, value);
  else
    PageSettingSet(id, value, *page_index);
}

void
PageSettingApplyGlobalBaseline() noexcept
{
  LogFmt("perPage: ApplyGlobalBaseline ({} settings)",
         PageSettingRegistry::Count());
  for (unsigned i = 0; i < PageSettingRegistry::Count(); ++i) {
    const auto id = PageSettingId(i);
    const auto &desc = TerrainDisplaySetting::Get(id);
    const int value = TerrainDisplaySetting::LoadGlobal(id);
    const int live = TerrainDisplaySetting::GetLive(id);
    LogFmt("perPage:   baseline '{}' = {} (live was {})",
           desc.label, value, live);
    TerrainDisplaySetting::SetLive(id, value);
  }
}

void
PageSettingApplyPageOverrides(unsigned page_index) noexcept
{
  if (page_index >= PageSettings::MAX_PAGES) {
    LogFmt("perPage: ApplyPageOverrides skip bad index={}", page_index);
    return;
  }

  const auto &overrides =
    CommonInterface::GetUISettings().pages.overrides[page_index];

  LogFmt("perPage: ApplyPageOverrides page={} n_items={}",
         page_index, overrides.n_items);

  for (unsigned i = 0; i < overrides.n_items; ++i) {
    const auto &item = overrides.items[i];
    const auto &desc = TerrainDisplaySetting::Get(item.id);

    if (item.value == PageSettingOverrides::INHERIT) {
      LogFmt("perPage:   override '{}' inherit (skip)", desc.label);
      continue;
    }

    if (!TerrainDisplaySetting::IsValidValue(item.id, item.value)) {
      LogFmt("perPage:   override '{}' value={} invalid (skip)",
             desc.label, item.value);
      continue;
    }

    LogFmt("perPage:   override '{}' = {}", desc.label, item.value);
    TerrainDisplaySetting::SetLive(item.id, item.value);
  }
}
