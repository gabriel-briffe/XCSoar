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
SortByLabel(PageSettingId *begin, PageSettingId *end,
            FilterLabelFn label) noexcept
{
  std::sort(begin, end,
            [label](PageSettingId a, PageSettingId b) noexcept {
              const char *const la = label(a);
              const char *const lb = label(b);
              if (la == nullptr || lb == nullptr)
                return la != nullptr;
#ifdef _MSC_VER
              return _stricmp(la, lb) < 0;
#else
              return strcasecmp(la, lb) < 0;
#endif
            });
}

[[nodiscard]]
static int
CompareCStringCaseInsensitive(const char *a, const char *b) noexcept
{
#ifdef _MSC_VER
  return _stricmp(a, b);
#else
  return strcasecmp(a, b);
#endif
}

int
CompareSectionAndLabel(const char *section_a, const char *label_a,
                       const char *section_b, const char *label_b) noexcept
{
  const char *const key_a = section_a != nullptr ? section_a : "";
  const char *const key_b = section_b != nullptr ? section_b : "";

  const int section_cmp = CompareCStringCaseInsensitive(key_a, key_b);
  if (section_cmp != 0)
    return section_cmp;

  return CompareCStringCaseInsensitive(label_a, label_b);
}

} // namespace PageSettingFilterCatalog
