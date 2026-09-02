// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Airspace/AirspaceComputerSettings.hpp"
#include "PageSettingDescriptor.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"

#include <cstdint>

/**
 * Airspace settings shared by the Airspace config panel and per-page
 * overrides (display, warnings, and per-class filters).  Class colours
 * remain outside this catalog.
 */
namespace AirspaceDisplaySetting {

struct Bundle {
  AirspaceRendererSettings airspace;
  AirspaceComputerSettings computer;
  bool enable_airspace_warning_dialog;
  /** Always present for catalog stability across canvas backends. */
  bool transparency;
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
IsClassFilter(PageSettingId id) noexcept
{
  return unsigned(id) >= unsigned(PageSettingId::AIRSPACE_CLASS_FILTER_BEGIN) &&
         unsigned(id) < unsigned(PageSettingId::COUNT);
}

[[nodiscard]]
constexpr AirspaceClass
ClassFromId(PageSettingId id) noexcept
{
  return AirspaceClass(unsigned(id) -
                       unsigned(PageSettingId::AIRSPACE_CLASS_FILTER_BEGIN) +
                       1);
}

} // namespace AirspaceDisplaySetting
