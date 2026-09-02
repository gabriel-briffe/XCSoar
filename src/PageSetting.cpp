// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSetting.hpp"
#include "PageSettingDescriptor.hpp"
#include "ActionInterface.hpp"
#include "Interface.hpp"
#include "MainWindow.hpp"
#include "MapSettings.hpp"
#include "PageSettingModule.hpp"
#include "PageSettings.hpp"
#include "Profile/Current.hpp"
#include "Profile/PageProfile.hpp"
#include "UISettings.hpp"
#include "UIState.hpp"
#include "Look/Look.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"
#include "Engine/Airspace/AirspaceClass.hpp"

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

bool
PageSettingOverrides::EqualsUnordered(const PageSettingOverrides &other) const noexcept
{
  if (n_items != other.n_items)
    return false;

  for (unsigned i = 0; i < n_items; ++i) {
    const int *v = other.FindValue(items[i].id);
    if (v == nullptr || *v != items[i].value)
      return false;
  }

  return true;
}

bool
PageOnlyCommands::Contains(PageSettingId id) const noexcept
{
  for (unsigned i = 0; i < n_items; ++i)
    if (ids[i] == id)
      return true;
  return false;
}

bool
PageOnlyCommands::Add(PageSettingId id) noexcept
{
  if (Contains(id))
    return false;
  if (n_items >= MAX_ITEMS)
    return false;

  ids[n_items++] = id;
  return true;
}

bool
PageOnlyCommands::Remove(PageSettingId id) noexcept
{
  for (unsigned i = 0; i < n_items; ++i) {
    if (ids[i] != id)
      continue;

    for (unsigned j = i + 1; j < n_items; ++j)
      ids[j - 1] = ids[j];
    --n_items;
    return true;
  }
  return false;
}

bool
PageOnlyCommands::operator==(const PageOnlyCommands &other) const noexcept
{
  if (n_items != other.n_items)
    return false;

  for (unsigned i = 0; i < n_items; ++i)
    if (!other.Contains(ids[i]))
      return false;

  return true;
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
  return PageSettingModuleRegistry::GetById(id).get(id);
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  unsigned offset = 0;
  for (unsigned m = 0; m < PageSettingModuleRegistry::Count(); ++m) {
    const auto &module = PageSettingModuleRegistry::Get(m);
    const unsigned n = module.count();
    if (index < offset + n)
      return module.get(PageSettingId(unsigned(module.id_start) + index -
                                      offset));
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
  const auto &module = PageSettingModuleRegistry::Get(group);
  return module.get(PageSettingId(unsigned(module.id_start) + index));
}

} // namespace PageSettingRegistry

void
PageSettingReinitialiseTrailLookIfChanged(const TrailSettings &before) noexcept
{
  const TrailSettings &after = CommonInterface::GetMapSettings().trail;
  if (before.type == after.type &&
      before.scaling_enabled == after.scaling_enabled)
    return;

  if (CommonInterface::main_window != nullptr)
    CommonInterface::main_window->SetLook().map.trail.Initialise(after);
}

void
PageSettingReinitialiseWaypointLookIfChanged(
  const WaypointRendererSettings &before) noexcept
{
  const WaypointRendererSettings &after =
    CommonInterface::GetMapSettings().waypoint;
  if (before.landable_style == after.landable_style)
    return;

  if (CommonInterface::main_window != nullptr)
    CommonInterface::main_window->SetLook().map.waypoint.Reinitialise(after);
}

void
PageSettingReinitialiseAirspaceLookIfChanged(
  const AirspaceRendererSettings &before) noexcept
{
  const AirspaceRendererSettings &after =
    CommonInterface::GetMapSettings().airspace;

  for (unsigned i = 1; i < AIRSPACECLASSCOUNT; ++i) {
    if (before.classes[i].fill_color != after.classes[i].fill_color ||
        before.classes[i].border_color != after.classes[i].border_color ||
        before.classes[i].border_width != after.classes[i].border_width ||
        before.classes[i].fill_mode != after.classes[i].fill_mode) {
      if (CommonInterface::main_window != nullptr)
        CommonInterface::main_window->SetLook().map.airspace.Reinitialise(after);
      return;
    }
  }
}

void
PageSettingNotifyLive() noexcept
{
  ActionInterface::SendMapSettings(true);
}

bool
PageSettingIsPageOnlyActive(PageSettingId id) noexcept
{
  const PagesState &state = CommonInterface::GetUIState().pages;
  if (state.special_page.IsDefined())
    return false;
  if (state.current_index >= PageSettings::MAX_PAGES)
    return false;

  return CommonInterface::GetUISettings().pages
    .page_only_commands[state.current_index].Contains(id);
}

void
PageSettingApplyCommand(PageSettingId id, int value) noexcept
{
  const auto &module = PageSettingModuleRegistry::GetById(id);
  if (!module.is_valid_value(id, value))
    return;

  if (value == PageSettingOverrides::INHERIT)
    value = module.load_global(id);

  if (PageSettingIsPageOnlyActive(id)) {
    const unsigned page_index =
      CommonInterface::GetUIState().pages.current_index;
    auto &pages = CommonInterface::SetUISettings().pages;
    pages.overrides[page_index].SetValue(id, value);
    Profile::Save(Profile::map, pages.overrides[page_index], page_index);

    const TrailSettings old_trail = CommonInterface::GetMapSettings().trail;
    const WaypointRendererSettings old_waypoint =
      CommonInterface::GetMapSettings().waypoint;
    const AirspaceRendererSettings old_airspace =
      CommonInterface::GetMapSettings().airspace;

    module.set_live(id, value);
    PageSettingReinitialiseTrailLookIfChanged(old_trail);
    PageSettingReinitialiseWaypointLookIfChanged(old_waypoint);
    PageSettingReinitialiseAirspaceLookIfChanged(old_airspace);
    PageSettingNotifyLive();
    return;
  }

  PageSettingSet(id, value);
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
  if (!module.is_valid_value(id, value))
    return;

  if (value == PageSettingOverrides::INHERIT)
    value = module.load_global(id);

  const TrailSettings old_trail = CommonInterface::GetMapSettings().trail;
  const WaypointRendererSettings old_waypoint =
    CommonInterface::GetMapSettings().waypoint;
  const AirspaceRendererSettings old_airspace =
    CommonInterface::GetMapSettings().airspace;

  module.set_live(id, value);
  module.save_global(id, value);
  PageSettingReinitialiseTrailLookIfChanged(old_trail);
  PageSettingReinitialiseWaypointLookIfChanged(old_waypoint);
  PageSettingReinitialiseAirspaceLookIfChanged(old_airspace);
  PageSettingNotifyLive();
}

void
PageSettingSet(PageSettingId id, int value, unsigned page_index) noexcept
{
  if (!PageSettingModuleRegistry::GetById(id).is_valid_value(id, value))
    return;

  auto &pages = CommonInterface::SetUISettings().pages;
  if (page_index >= PageSettings::MAX_PAGES)
    return;

  pages.overrides[page_index].SetValue(id, value);
}

namespace {

bool have_applied_page = false;
unsigned last_applied_page = 0;

void
RestoreOverridesToGlobal(unsigned page_index) noexcept
{
  if (page_index >= PageSettings::MAX_PAGES)
    return;

  const auto &overrides =
    CommonInterface::GetUISettings().pages.overrides[page_index];

  for (unsigned i = 0; i < overrides.n_items; ++i) {
    const auto id = overrides.items[i].id;
    const auto &module = PageSettingModuleRegistry::GetById(id);
    /* Ephemeral globals fall back to the catalog default. */
    const int value = module.get(id).profile_key.empty()
      ? module.get(id).profile_default
      : module.load_global(id);
    module.set_live(id, value);
  }
}

} // namespace

void
PageSettingApplyGlobalBaseline() noexcept
{
  for (unsigned m = 0; m < PageSettingModuleRegistry::Count(); ++m) {
    const auto &module = PageSettingModuleRegistry::Get(m);
    for (unsigned i = 0; i < module.count(); ++i) {
      const auto id = PageSettingId(unsigned(module.id_start) + i);
      /* Settings without a global profile key are ephemeral (e.g.
         airspace show toggle) — do not stomp the live value. */
      if (module.get(id).profile_key.empty())
        continue;

      const int value = module.load_global(id);
      if (module.get_live(id) != value)
        module.set_live(id, value);
    }
  }
}

void
PageSettingApplyPageOverrides(unsigned page_index) noexcept
{
  if (page_index >= PageSettings::MAX_PAGES)
    return;

  const auto &overrides =
    CommonInterface::GetUISettings().pages.overrides[page_index];

  for (unsigned i = 0; i < overrides.n_items; ++i) {
    const auto &item = overrides.items[i];
    const auto &module = PageSettingModuleRegistry::GetById(item.id);

    if (item.value == PageSettingOverrides::INHERIT)
      continue;

    if (!module.is_valid_value(item.id, item.value))
      continue;

    module.set_live(item.id, item.value);
  }
}

/**
 * Apply global baseline + page overrides with a cheap page-switch path:
 * when leaving page A for B, restore only A's override ids, then apply B.
 * Same-page refresh (or first apply) still does a full baseline.
 */
void
PageSettingApplyDisplaySettings(unsigned page_index) noexcept
{
  if (page_index >= PageSettings::MAX_PAGES)
    return;

  if (have_applied_page && last_applied_page != page_index)
    RestoreOverridesToGlobal(last_applied_page);
  else
    PageSettingApplyGlobalBaseline();

  PageSettingApplyPageOverrides(page_index);
  last_applied_page = page_index;
  have_applied_page = true;
}
