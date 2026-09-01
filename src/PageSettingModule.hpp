// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSetting.hpp"
#include "PageSettingDescriptor.hpp"

struct PageSettingModule {
  PageSettingGroup group;

  /** UI label for the group (N_(); gettext when showing). */
  const char *label;

  unsigned (*count)() noexcept;
  const PageSettingDescriptor &(*get_by_index)(unsigned index) noexcept;
  const PageSettingDescriptor &(*get_by_id)(PageSettingId id) noexcept;

  bool (*is_valid_value)(PageSettingId id, int value) noexcept;
  int (*get_live)(PageSettingId id) noexcept;
  void (*set_live)(PageSettingId id, int value) noexcept;
  int (*load_global)(PageSettingId id) noexcept;
  void (*save_global)(PageSettingId id, int value) noexcept;
};

namespace PageSettingModuleRegistry {

[[nodiscard]]
unsigned
Count() noexcept;

[[nodiscard]]
const PageSettingModule &
Get(unsigned index) noexcept;

[[nodiscard]]
const PageSettingModule &
Get(PageSettingGroup group) noexcept;

[[nodiscard]]
const PageSettingModule &
GetById(PageSettingId id) noexcept;

[[nodiscard]]
const char *
GetLabel(PageSettingGroup group) noexcept;

} // namespace PageSettingModuleRegistry

[[nodiscard]] inline PageSettingGroup
PageSettingGroupForId(PageSettingId id) noexcept
{
  return unsigned(id) < PageSettingTerrainCount
    ? PageSettingGroup::TERRAIN
    : PageSettingGroup::MAP;
}
