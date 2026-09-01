// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "DisplaySettingConfigPanel.hpp"

#include "Form/DataField/Listener.hpp"
#include "Language/Language.hpp"
#include "PageSettingCatalog.hpp"
#include "Widget/RowFormWidget.hpp"

namespace DisplaySettingConfigPanel {

void
AddBooleanRow(RowFormWidget &form, const PageSettingDescriptor &desc,
              bool value, DataFieldListener *listener) noexcept
{
  form.AddBoolean(PageSettingCatalog::GettextOptional(desc.label),
                  PageSettingCatalog::GettextOptional(desc.help_global),
                  value, listener);
}

void
AddEnumRow(RowFormWidget &form, const PageSettingDescriptor &desc,
           unsigned value, DataFieldListener *listener) noexcept
{
  form.AddEnum(PageSettingCatalog::GettextOptional(desc.label),
               PageSettingCatalog::GettextOptional(desc.help_global),
               desc.choices, value, listener);
}

void
AddIntegerRow(RowFormWidget &form, const PageSettingDescriptor &desc,
              int value, DataFieldListener *listener) noexcept
{
  form.AddInteger(PageSettingCatalog::GettextOptional(desc.label),
                  PageSettingCatalog::GettextOptional(desc.help_global),
                  "%d %%", "%d", desc.int_min, desc.int_max, desc.int_step,
                  value, listener);
}

void
AddRow(RowFormWidget &form, const PageSettingDescriptor &desc, int value,
       DataFieldListener *listener) noexcept
{
  switch (desc.type) {
  case PageSettingType::BOOL:
    AddBooleanRow(form, desc, value != 0, listener);
    break;
  case PageSettingType::ENUM:
    AddEnumRow(form, desc, unsigned(value), listener);
    break;
  case PageSettingType::INTEGER:
    AddIntegerRow(form, desc, value, listener);
    break;
  }
}

} // namespace DisplaySettingConfigPanel
