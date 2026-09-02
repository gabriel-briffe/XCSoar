// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSettingFilterCatalog.hpp"

#include "util/StringAPI.hxx"

#include <algorithm>
#include <cstring>

namespace PageSettingFilterCatalog {

void
CopyBase(PageSettingDescriptor *dest,
         const PageSettingDescriptor *base,
         unsigned base_count) noexcept
{
  std::memcpy(dest, base, base_count * sizeof(PageSettingDescriptor));
}

PageSettingDescriptor
MakeBoolFilter(PageSettingId id, const char *label, const char *help,
               const char *override_key, std::string_view profile_key,
               PageSettingBundleField bundle_field,
               int profile_default) noexcept
{
  return PageSettingCatalog::CatalogBool(
    id, label, help, override_key, profile_key, bundle_field,
    profile_default, SECTION_FILTERS);
}

PageSettingDescriptor
MakeEnumFilter(PageSettingId id, const char *label, const char *help,
               const char *override_key,
               PageSettingBundleField bundle_field,
               ProfileWireFormat wire, int profile_default,
               const StaticEnumChoice *choices) noexcept
{
  return PageSettingCatalog::CatalogEnum(
    id, label, help, override_key, {}, bundle_field, wire,
    profile_default, choices, SECTION_FILTERS);
}

void
FillConsecutiveIds(PageSettingId *order, PageSettingId first_id,
                   unsigned count) noexcept
{
  for (unsigned i = 0; i < count; ++i)
    order[i] = PageSettingId(unsigned(first_id) + i);
}

void
InitSortedOrder(PageSettingId *order, unsigned count,
                FilterLabelFn label) noexcept
{
  std::sort(order, order + count,
            [label](PageSettingId a, PageSettingId b) noexcept {
              return StringCompareIgnoreCase(label(a), label(b)) < 0;
            });
}

int
CompareSectionAndLabel(const char *section_a, const char *label_a,
                       const char *section_b, const char *label_b) noexcept
{
  const char *const key_a = section_a != nullptr ? section_a : "";
  const char *const key_b = section_b != nullptr ? section_b : "";

  const int section_cmp = StringCompareIgnoreCase(key_a, key_b);
  if (section_cmp != 0)
    return section_cmp;

  return StringCompareIgnoreCase(label_a, label_b);
}

} // namespace PageSettingFilterCatalog
