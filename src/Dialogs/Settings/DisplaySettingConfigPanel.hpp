// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettingDescriptor.hpp"
#include "Widget/RowFormWidget.hpp"

class DataFieldListener;

/**
 * Helpers for Map Display / Airspace config panels driven by a
 * #PageSettingDescriptor catalog.
 */
namespace DisplaySettingConfigPanel {

[[nodiscard]]
constexpr unsigned
CatalogRow(PageSettingId id, unsigned id_start) noexcept
{
  return unsigned(id) - id_start;
}

void
AddBooleanRow(RowFormWidget &form, const PageSettingDescriptor &desc,
              bool value, DataFieldListener *listener=nullptr) noexcept;

void
AddEnumRow(RowFormWidget &form, const PageSettingDescriptor &desc,
           unsigned value, DataFieldListener *listener=nullptr) noexcept;

void
AddIntegerRow(RowFormWidget &form, const PageSettingDescriptor &desc,
              int value, DataFieldListener *listener=nullptr) noexcept;

void
AddRow(RowFormWidget &form, const PageSettingDescriptor &desc, int value,
       DataFieldListener *listener=nullptr) noexcept;

/**
 * Read form rows [0, @p count) into @p bundle for consecutive catalog
 * ids starting at @p id_start.
 */
template<typename Bundle>
void
SyncBundleFromForm(RowFormWidget &form, Bundle &bundle,
                   unsigned id_start, unsigned count,
                   const PageSettingDescriptor &(*get)(PageSettingId) noexcept,
                   void (*set_value)(Bundle &, PageSettingId, int) noexcept) noexcept
{
  for (unsigned i = 0; i < count; ++i) {
    const auto id = PageSettingId(id_start + i);
    const auto &desc = get(id);

    switch (desc.type) {
    case PageSettingType::BOOL:
      set_value(bundle, id, form.GetValueBoolean(i) ? 1 : 0);
      break;

    case PageSettingType::ENUM:
      set_value(bundle, id, int(form.GetValueEnum(i)));
      break;

    case PageSettingType::INTEGER:
      set_value(bundle, id, form.GetValueInteger(i));
      break;

    case PageSettingType::COLOR:
      /* Colours are not hosted in RowForm catalog panels. */
      assert(false);
      break;
    }
  }
}

/**
 * Append catalog rows for ids [@p id_start, @p id_start + @p count).
 *
 * @param is_expert_row optional; marks expert rows when true
 * @param needs_listener optional; attaches @p listener when true
 * @param listener_control if @p needs_listener is null, attach @p listener
 *        only to this control index (~0u = none)
 */
template<typename Bundle>
unsigned
AddCatalogRows(RowFormWidget &form, const Bundle &bundle,
               unsigned id_start, unsigned count,
               const PageSettingDescriptor &(*get)(PageSettingId) noexcept,
               int (*get_value)(const Bundle &, PageSettingId) noexcept,
               bool (*is_expert_row)(unsigned) noexcept = nullptr,
               DataFieldListener *listener = nullptr,
               bool (*needs_listener)(unsigned) noexcept = nullptr,
               unsigned listener_control = ~0u) noexcept
{
  for (unsigned i = 0; i < count; ++i) {
    const auto id = PageSettingId(id_start + i);
    const auto &desc = get(id);

    DataFieldListener *row_listener = nullptr;
    if (listener != nullptr) {
      if (needs_listener != nullptr) {
        if (needs_listener(i))
          row_listener = listener;
      } else if (i == listener_control) {
        row_listener = listener;
      }
    }

    AddRow(form, desc, get_value(bundle, id), row_listener);

    if (is_expert_row != nullptr && is_expert_row(i))
      form.SetExpertRow(i);
  }

  return count;
}

/**
 * Append controls after a catalog block.  @p after_control is the
 * value returned by #AddCatalogRows (index of the first non-catalog
 * row).  @p add receives that base index for offset enums.
 */
template<typename Fn>
void
AddNonCatalogRowsAfter(unsigned after_control, Fn &&add) noexcept
{
  std::forward<Fn>(add)(after_control);
}

} // namespace DisplaySettingConfigPanel
