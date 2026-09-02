// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettingCatalog.hpp"
#include "PageSettingDescriptor.hpp"

#include <string_view>

/**
 * Helpers for appending per-page filter rows to a display-setting catalog.
 */
namespace PageSettingFilterCatalog {

constexpr const char *SECTION_FILTERS = N_("Filters");

void
CopyBase(PageSettingDescriptor *dest,
         const PageSettingDescriptor *base,
         unsigned base_count) noexcept;

[[nodiscard]]
PageSettingDescriptor
MakeBoolFilter(PageSettingId id, const char *label, const char *help,
               const char *override_key, std::string_view profile_key,
               PageSettingBundleField bundle_field,
               int profile_default = 1) noexcept;

[[nodiscard]]
PageSettingDescriptor
MakeEnumFilter(PageSettingId id, const char *label, const char *help,
               const char *override_key,
               PageSettingBundleField bundle_field,
               ProfileWireFormat wire, int profile_default,
               const StaticEnumChoice *choices) noexcept;

using FilterLabelFn = const char *(*)(PageSettingId id) noexcept;

void
FillConsecutiveIds(PageSettingId *order, PageSettingId first_id,
                   unsigned count) noexcept;

/**
 * Sort @p order[0, @p count) alphabetically by @p label (case
 * insensitive).  Populate @p order before calling.
 */
void
InitSortedOrder(PageSettingId *order, unsigned count,
                FilterLabelFn label) noexcept;

/**
 * Compare two catalog entries for picker / list ordering: section name
 * (empty when @p section is null), then label case-insensitively.
 */
[[nodiscard]]
int
CompareSectionAndLabel(const char *section_a, const char *label_a,
                       const char *section_b, const char *label_b) noexcept;

} // namespace PageSettingFilterCatalog
