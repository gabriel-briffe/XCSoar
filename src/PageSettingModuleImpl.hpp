// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettingCatalog.hpp"
#include "PageSettingProfile.hpp"
#include "util/Macros.hpp"

#include <cassert>

/**
 * Shared get/set/catalog logic for a #PageSettingModule group.
 *
 * Each group's .cpp supplies the catalog, field accessors, and
 * ReadLive/ApplyLive; optional custom LoadGlobal/SaveGlobal bundle hooks
 * cover terrain (non-uniform profile keys) and similar cases.
 */
namespace PageSettingModuleImpl {

template<typename Bundle, typename FieldEnum,
         const PageSettingDescriptor *Catalog, unsigned CatalogSize,
         unsigned IdStart,
         FieldEnum (*FieldFromDescriptor)(const PageSettingDescriptor &) noexcept,
         int (*GetField)(const Bundle &, FieldEnum) noexcept,
         void (*SetField)(Bundle &, FieldEnum, int) noexcept,
         void (*ReadLive)(Bundle &) noexcept,
         void (*ApplyLive)(const Bundle &) noexcept>
struct Module {
  static unsigned
  Count() noexcept
  {
    return CatalogSize;
  }

  [[nodiscard]]
  static const PageSettingDescriptor &
  Get(PageSettingId id) noexcept
  {
    assert(unsigned(id) >= IdStart);
    assert(unsigned(id) < IdStart + CatalogSize);
    return Catalog[unsigned(id) - IdStart];
  }

  [[nodiscard]]
  static const PageSettingDescriptor &
  Get(unsigned index) noexcept
  {
    assert(index < CatalogSize);
    return Catalog[index];
  }

  [[nodiscard]]
  static bool
  IsValidValue(PageSettingId id, int value) noexcept
  {
    return PageSettingCatalog::IsValidValue(Get(id), value);
  }

  [[nodiscard]]
  static int
  GetLive(PageSettingId id) noexcept
  {
    return PageSettingCatalog::GetLive<Bundle>(id, ReadLive, GetValue);
  }

  static void
  SetLive(PageSettingId id, int value) noexcept
  {
    PageSettingCatalog::SetLive<Bundle>(id, value, ReadLive, ApplyLive,
                                        SetValue, IsValidValue);
  }

  [[nodiscard]]
  static int
  LoadGlobal(PageSettingId id) noexcept
  {
    return PageSettingProfile::Load(Get(id));
  }

  static void
  SaveGlobal(PageSettingId id, int value) noexcept
  {
    if (!IsValidValue(id, value))
      return;

    PageSettingProfile::Save(Get(id), value);
  }

  [[nodiscard]]
  static int
  GetValue(const Bundle &bundle, PageSettingId id) noexcept
  {
    return GetField(bundle, FieldFromDescriptor(Get(id)));
  }

  static void
  SetValue(Bundle &bundle, PageSettingId id, int value) noexcept
  {
    if (!IsValidValue(id, value))
      return;

    SetField(bundle, FieldFromDescriptor(Get(id)), value);
  }

  static void
  LoadGlobalBundle(Bundle &bundle) noexcept
  {
    for (unsigned i = 0; i < CatalogSize; ++i) {
      const auto id = PageSettingId(IdStart + i);
      SetValue(bundle, id, LoadGlobal(id));
    }
  }

  [[nodiscard]]
  static bool
  SaveGlobalBundle(const Bundle &current, const Bundle &initial) noexcept
  {
    bool changed = false;

    for (unsigned i = 0; i < CatalogSize; ++i) {
      const auto id = PageSettingId(IdStart + i);
      if (GetValue(current, id) == GetValue(initial, id))
        continue;

      SaveGlobal(id, GetValue(current, id));
      changed = true;
    }

    return changed;
  }
};

} // namespace PageSettingModuleImpl
