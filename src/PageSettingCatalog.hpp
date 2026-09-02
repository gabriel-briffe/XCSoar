// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettingDescriptor.hpp"
#include "DisplaySettingChoices.hpp"

class DataFieldEnum;

namespace PageSettingCatalog {

[[nodiscard]]
const char *
GettextOptional(const char *text) noexcept;

[[nodiscard]]
bool
IsValidValue(const PageSettingDescriptor &desc, int value) noexcept;

void
FillDataFieldEnum(DataFieldEnum &df, const PageSettingDescriptor &desc,
                  int value) noexcept;

template<typename Bundle>
[[nodiscard]]
int
GetLive(PageSettingId id,
        void (*read_live)(Bundle &) noexcept,
        int (*get_value)(const Bundle &, PageSettingId) noexcept) noexcept
{
  Bundle bundle;
  read_live(bundle);
  return get_value(bundle, id);
}

template<typename Bundle>
void
SetLive(PageSettingId id, int value,
        void (*read_live)(Bundle &) noexcept,
        void (*apply_live)(const Bundle &) noexcept,
        void (*set_value)(Bundle &, PageSettingId, int) noexcept,
        bool (*is_valid)(PageSettingId, int) noexcept) noexcept
{
  if (!is_valid(id, value))
    return;

  Bundle bundle;
  read_live(bundle);
  set_value(bundle, id, value);
  apply_live(bundle);
}

[[nodiscard]]
constexpr PageSettingDescriptor
CatalogBool(PageSettingId id, const char *label, const char *help,
            const char *override_key, std::string_view profile_key,
            PageSettingBundleField bundle_field,
            int profile_default = 1,
            const char *section = nullptr) noexcept
{
  return {
    id, PageSettingType::BOOL, label, help, override_key, profile_key,
    bundle_field, ProfileWireFormat::BOOL, profile_default,
    enabled_disabled_choices, 0, 0, 0, nullptr, section,
  };
}

[[nodiscard]]
constexpr PageSettingDescriptor
CatalogEnum(PageSettingId id, const char *label, const char *help,
              const char *override_key, std::string_view profile_key,
              PageSettingBundleField bundle_field, ProfileWireFormat wire,
              int profile_default,
              const StaticEnumChoice *choices,
              const char *section = nullptr) noexcept
{
  return {
    id, PageSettingType::ENUM, label, help, override_key, profile_key,
    bundle_field, wire, profile_default, choices, 0, 0, 0, nullptr, section,
  };
}

[[nodiscard]]
constexpr PageSettingDescriptor
CatalogInteger(PageSettingId id, const char *label, const char *help,
               const char *override_key, std::string_view profile_key,
               PageSettingBundleField bundle_field, ProfileWireFormat wire,
               int profile_default,
               int int_min, int int_max, int int_step,
               const char *int_format = "%d %%",
               const char *section = nullptr) noexcept
{
  return {
    id, PageSettingType::INTEGER, label, help, override_key, profile_key,
    bundle_field, wire, profile_default,
    nullptr, int_min, int_max, int_step, int_format, section,
  };
}

/**
 * Packed RGB24 colour override (0..0xffffff).  Not edited via
 * #FillDataFieldEnum — UI uses a colour dialog / swatch.
 */
[[nodiscard]]
constexpr PageSettingDescriptor
CatalogColor(PageSettingId id, const char *label, const char *help,
             const char *override_key,
             PageSettingBundleField bundle_field,
             int profile_default,
             const char *section = nullptr) noexcept
{
  return {
    id, PageSettingType::COLOR, label, help, override_key, {},
    bundle_field, ProfileWireFormat::INT, profile_default,
    nullptr, 0, 0xffffff, 1, "%06X", section,
  };
}

} // namespace PageSettingCatalog
