// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSetting.hpp"
#include "PageSettingDescriptor.hpp"
#include "ActionInterface.hpp"
#include "Interface.hpp"
#include "LogFile.hpp"
#include "PageSettingModule.hpp"
#include "PageSettings.hpp"
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
  unsigned total = 0;
  for (unsigned i = 0; i < PageSettingModuleRegistry::Count(); ++i)
    total += PageSettingModuleRegistry::Get(i).count();
  return total;
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  return PageSettingModuleRegistry::GetById(id).get_by_id(id);
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  unsigned offset = 0;
  for (unsigned m = 0; m < PageSettingModuleRegistry::Count(); ++m) {
    const auto &module = PageSettingModuleRegistry::Get(m);
    const unsigned n = module.count();
    if (index < offset + n)
      return module.get_by_index(index - offset);
    offset += n;
  }

  assert(index < Count());
  gcc_unreachable();
}

bool
IsValidValue(const PageSettingDescriptor &desc, int value) noexcept
{
  return PageSettingModuleRegistry::GetById(desc.id).is_valid_value(desc.id,
                                                                  value);
}

unsigned
Count(PageSettingGroup group) noexcept
{
  return PageSettingModuleRegistry::Get(group).count();
}

const PageSettingDescriptor &
Get(PageSettingGroup group, unsigned index) noexcept
{
  return PageSettingModuleRegistry::Get(group).get_by_index(index);
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
  return PageSettingModuleRegistry::GetById(id).load_global(id);
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

  return PageSettingGet(id);
}

void
PageSettingSet(PageSettingId id, int value) noexcept
{
  const auto &module = PageSettingModuleRegistry::GetById(id);
  if (!module.is_valid_value(id, value)) {
    LogFmt("perPage: Set global reject id={} value={} (invalid)",
           unsigned(id), value);
    return;
  }

  if (value == PageSettingOverrides::INHERIT)
    value = module.load_global(id);

  const auto &desc = module.get_by_id(id);
  LogFmt("perPage: Set global '{}' value={}", desc.label, value);
  module.set_live(id, value);
  module.save_global(id, value);
  PageSettingNotifyLive();
}

void
PageSettingSet(PageSettingId id, int value, unsigned page_index) noexcept
{
  if (!PageSettingModuleRegistry::GetById(id).is_valid_value(id, value)) {
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

  const auto &desc = PageSettingModuleRegistry::GetById(id).get_by_id(id);
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
  for (unsigned m = 0; m < PageSettingModuleRegistry::Count(); ++m) {
    const auto &module = PageSettingModuleRegistry::Get(m);
    for (unsigned i = 0; i < module.count(); ++i) {
      const auto &desc = module.get_by_index(i);
      const auto id = desc.id;
      const int value = module.load_global(id);
      const int live = module.get_live(id);
      LogFmt("perPage:   baseline '{}' = {} (live was {})",
             desc.label, value, live);
      module.set_live(id, value);
    }
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
    const auto &module = PageSettingModuleRegistry::GetById(item.id);
    const auto &desc = module.get_by_id(item.id);

    if (item.value == PageSettingOverrides::INHERIT) {
      LogFmt("perPage:   override '{}' inherit (skip)", desc.label);
      continue;
    }

    if (!module.is_valid_value(item.id, item.value)) {
      LogFmt("perPage:   override '{}' value={} invalid (skip)",
             desc.label, item.value);
      continue;
    }

    LogFmt("perPage:   override '{}' = {}", desc.label, item.value);
    module.set_live(item.id, item.value);
  }
}
