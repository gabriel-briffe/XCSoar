// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSettingCatalog.hpp"

#include "Form/DataField/Enum.hpp"
#include "Language/Language.hpp"
#include "PageSetting.hpp"
#include "util/StringFormat.hpp"

#include <cassert>

namespace PageSettingCatalog {

bool
IsValidValue(const PageSettingDescriptor &desc, int value) noexcept
{
  if (value == PageSettingOverrides::INHERIT)
    return true;

  if (desc.type == PageSettingType::INTEGER) {
    if (value < desc.int_min || value > desc.int_max)
      return false;
    if (desc.int_step <= 0)
      return true;
    return (value - desc.int_min) % desc.int_step == 0;
  }

  assert(desc.choices != nullptr);
  for (const StaticEnumChoice *c = desc.choices;
       c->display_string != nullptr; ++c)
    if (int(c->id) == value)
      return true;
  return false;
}

void
FillDataFieldEnum(DataFieldEnum &df, const PageSettingDescriptor &desc,
                  int value) noexcept
{
  df.ClearChoices();

  if (desc.type == PageSettingType::INTEGER) {
    char label[16];
    for (int v = desc.int_min; v <= desc.int_max; v += desc.int_step) {
      StringFormat(label, sizeof(label), "%d %%", v);
      df.AddChoice(unsigned(v), label);
    }
  } else {
    assert(desc.choices != nullptr);
    for (const StaticEnumChoice *c = desc.choices;
         c->display_string != nullptr; ++c)
      df.AddChoice(c->id, gettext(c->display_string), nullptr,
                   c->help != nullptr ? gettext(c->help) : nullptr);
  }

  df.SetValue(unsigned(value));
}

} // namespace PageSettingCatalog
