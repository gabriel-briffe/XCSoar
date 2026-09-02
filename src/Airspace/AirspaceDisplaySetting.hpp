// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Airspace/AirspaceComputerSettings.hpp"
#include "PageSettingDescriptor.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"

#include <cstdint>

/**
 * Airspace settings shared by the Airspace config panel and per-page
 * overrides (display, warnings, filters, and class colours / style).
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
         unsigned(id) < unsigned(PageSettingId::AIRSPACE_CLASS_FILL_COLOR_BEGIN);
}

[[nodiscard]]
constexpr bool
IsClassFillColor(PageSettingId id) noexcept
{
  return unsigned(id) >= unsigned(PageSettingId::AIRSPACE_CLASS_FILL_COLOR_BEGIN) &&
         unsigned(id) < unsigned(PageSettingId::AIRSPACE_CLASS_BORDER_COLOR_BEGIN);
}

[[nodiscard]]
constexpr bool
IsClassBorderColor(PageSettingId id) noexcept
{
  return unsigned(id) >= unsigned(PageSettingId::AIRSPACE_CLASS_BORDER_COLOR_BEGIN) &&
         unsigned(id) < unsigned(PageSettingId::AIRSPACE_CLASS_BORDER_WIDTH_BEGIN);
}

[[nodiscard]]
constexpr bool
IsClassBorderWidth(PageSettingId id) noexcept
{
  return unsigned(id) >= unsigned(PageSettingId::AIRSPACE_CLASS_BORDER_WIDTH_BEGIN) &&
         unsigned(id) < unsigned(PageSettingId::AIRSPACE_CLASS_FILL_MODE_BEGIN);
}

[[nodiscard]]
constexpr bool
IsClassFillMode(PageSettingId id) noexcept
{
  return unsigned(id) >= unsigned(PageSettingId::AIRSPACE_CLASS_FILL_MODE_BEGIN) &&
         unsigned(id) < unsigned(PageSettingId::COUNT);
}

[[nodiscard]]
constexpr AirspaceClass
ClassFromFilterId(PageSettingId id) noexcept
{
  return AirspaceClass(unsigned(id) -
                       unsigned(PageSettingId::AIRSPACE_CLASS_FILTER_BEGIN) +
                       1);
}

[[nodiscard]]
constexpr AirspaceClass
ClassFromFillColorId(PageSettingId id) noexcept
{
  return AirspaceClass(unsigned(id) -
                       unsigned(PageSettingId::AIRSPACE_CLASS_FILL_COLOR_BEGIN) +
                       1);
}

[[nodiscard]]
constexpr AirspaceClass
ClassFromBorderColorId(PageSettingId id) noexcept
{
  return AirspaceClass(unsigned(id) -
                       unsigned(PageSettingId::AIRSPACE_CLASS_BORDER_COLOR_BEGIN) +
                       1);
}

[[nodiscard]]
constexpr AirspaceClass
ClassFromBorderWidthId(PageSettingId id) noexcept
{
  return AirspaceClass(unsigned(id) -
                       unsigned(PageSettingId::AIRSPACE_CLASS_BORDER_WIDTH_BEGIN) +
                       1);
}

[[nodiscard]]
constexpr AirspaceClass
ClassFromFillModeId(PageSettingId id) noexcept
{
  return AirspaceClass(unsigned(id) -
                       unsigned(PageSettingId::AIRSPACE_CLASS_FILL_MODE_BEGIN) +
                       1);
}

[[nodiscard]]
constexpr PageSettingId
FillColorId(AirspaceClass cls) noexcept
{
  return PageSettingId(unsigned(PageSettingId::AIRSPACE_CLASS_FILL_COLOR_BEGIN) +
                       unsigned(cls) - 1);
}

[[nodiscard]]
constexpr PageSettingId
BorderColorId(AirspaceClass cls) noexcept
{
  return PageSettingId(unsigned(PageSettingId::AIRSPACE_CLASS_BORDER_COLOR_BEGIN) +
                       unsigned(cls) - 1);
}

[[nodiscard]]
constexpr PageSettingId
BorderWidthId(AirspaceClass cls) noexcept
{
  return PageSettingId(unsigned(PageSettingId::AIRSPACE_CLASS_BORDER_WIDTH_BEGIN) +
                       unsigned(cls) - 1);
}

[[nodiscard]]
constexpr PageSettingId
FillModeId(AirspaceClass cls) noexcept
{
  return PageSettingId(unsigned(PageSettingId::AIRSPACE_CLASS_FILL_MODE_BEGIN) +
                       unsigned(cls) - 1);
}

/**
 * True for any per-class colour / style catalog id (fill/border colour,
 * border width, fill mode).  The Pages UI collapses these into one row.
 */
[[nodiscard]]
constexpr bool
IsClassColor(PageSettingId id) noexcept
{
  return IsClassFillColor(id) || IsClassBorderColor(id) ||
         IsClassBorderWidth(id) || IsClassFillMode(id);
}

[[nodiscard]]
constexpr AirspaceClass
ClassFromColorId(PageSettingId id) noexcept
{
  if (IsClassFillColor(id))
    return ClassFromFillColorId(id);
  if (IsClassBorderColor(id))
    return ClassFromBorderColorId(id);
  if (IsClassBorderWidth(id))
    return ClassFromBorderWidthId(id);
  return ClassFromFillModeId(id);
}

[[nodiscard]]
bool
HasColorOverride(const PageSettingOverrides &overrides,
                 AirspaceClass cls) noexcept;

void
AddColorOverrides(PageSettingOverrides &overrides,
                  AirspaceClass cls) noexcept;

void
RemoveColorOverrides(PageSettingOverrides &overrides,
                     AirspaceClass cls) noexcept;

[[nodiscard]]
constexpr unsigned
FilterDialogRowCount() noexcept
{
  return PageSettingAirspaceClassFilterCount;
}

[[nodiscard]]
PageSettingId
FilterDialogRowId(unsigned row) noexcept;

} // namespace AirspaceDisplaySetting
