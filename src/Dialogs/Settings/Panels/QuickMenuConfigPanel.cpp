// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "QuickMenuConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Form/DataField/Enum.hpp"
#include "Input/InputEvents.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Menu/ButtonLabel.hpp"
#include "Menu/MenuData.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "UISettings.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StaticString.hxx"
#include "util/StringFormat.hpp"
#include "util/TruncateString.hpp"
#include "util/UTF8.hpp"

#include <cstddef>
#include <memory>

static void
FormatMenuChoiceLabel(const char *label, char *dest, size_t dest_size) noexcept
{
  if (dest_size == 0)
    return;

  char buffer[128];
  const auto expanded = ButtonLabel::Expand(label, std::span{buffer});
  const char *src = expanded.visible && expanded.text != nullptr
    ? expanded.text
    : label;

  /* Collapse newlines to spaces in a temp buffer, advancing by whole
     UTF-8 characters so CopyTruncateString never receives a split
     sequence. */
  char normalized[128];
  size_t j = 0;
  for (const char *p = src; *p != '\0' && j + 1 < sizeof(normalized);) {
    if (*p == '\n' || *p == '\r') {
      if (j > 0 && normalized[j - 1] != ' ')
        normalized[j++] = ' ';
      ++p;
      continue;
    }

    std::size_t len = SequenceLengthUTF8(*p);
    if (len == 0)
      len = 1;
    if (j + len >= sizeof(normalized))
      break;
    for (std::size_t i = 0; i < len; ++i)
      normalized[j++] = *p++;
  }
  normalized[j] = '\0';

  CopyTruncateString(dest, dest_size, normalized);
}

static void
FillQuickMenuChoices(DataFieldEnum &dfe) noexcept
{
  dfe.ClearChoices();
  dfe.AddChoice(0, _("(none)"));

  const Menu *menu = InputEvents::GetMenu("RemoteStick");
  if (menu == nullptr)
    return;

  for (unsigned i = 0; i < Menu::MAX_ITEMS; ++i) {
    const auto &item = (*menu)[i];
    if (!item.IsDefined() || item.label == nullptr)
      continue;

    char display[128];
    FormatMenuChoiceLabel(item.label, display, sizeof(display));
    if (display[0] == '\0')
      continue;

    dfe.AddChoice(i, display);
  }

  /* Keep "(none)" first; sort command labels A–Z. */
  if (dfe.Count() > 1)
    dfe.Sort(1);
}

std::unique_ptr<Widget>
CreateQuickMenuConfigPanel()
{
  const UISettings &settings = CommonInterface::GetUISettings();

  struct Fields {
    bool custom_menu;
    unsigned visible_count;
    unsigned items[UISettings::MAX_CUSTOM_QUICK_MENU];
    StaticString<8> captions[UISettings::MAX_CUSTOM_QUICK_MENU];
  };

  auto fields = std::make_shared<Fields>();
  fields->custom_menu = settings.custom_quick_menu;
  fields->visible_count = settings.custom_quick_menu_count > 0
    ? settings.custom_quick_menu_count
    : 1;
  for (unsigned i = 0; i < UISettings::MAX_CUSTOM_QUICK_MENU; ++i) {
    fields->items[i] = i < settings.custom_quick_menu_count
      ? settings.custom_quick_menu_items[i]
      : 0;
    fields->captions[i].Format("%u", i + 1);
  }

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  auto *page = list.get();
  list->AddGroup(nullptr);

  /* Linked switch so slot rows refresh when Custom menu flips. */
  {
    GroupedListWidget::ItemOptions options;
    options.toggle = true;
    options.checked = fields->custom_menu;
    options.help =
      _("When enabled, the Quick Menu shows only the commands "
        "selected below, in that order. When disabled, the full "
        "default Quick Menu is used; your selection is kept for "
        "when you turn this back on.");
    list->AddItem(_("Custom menu"), [fields, page] {
      fields->custom_menu = !fields->custom_menu;
      if (page->UpdateValues())
        page->UpdateLayout();
    }, options);
  }

  for (unsigned i = 0; i < UISettings::MAX_CUSTOM_QUICK_MENU; ++i) {
    const unsigned index = i;
    auto slot_shown = [fields, index] {
      return fields->custom_menu && index < fields->visible_count;
    };

    list->AddValue(fields->captions[i].c_str(), nullptr,
                   [fields, index, slot_shown](
                     GroupedListWidget::ValueState &state) {
                     state.hidden = !slot_shown();
                     DataFieldEnum dfe;
                     FillQuickMenuChoices(dfe);
                     dfe.SetValue(fields->items[index]);
                     state.text = dfe.GetAsDisplayString();
                   },
                   [fields, page, index] {
                     DataFieldEnum dfe;
                     FillQuickMenuChoices(dfe);
                     dfe.SetValue(fields->items[index]);
                     if (!ComboPicker(fields->captions[index].c_str(),
                                      dfe, nullptr))
                       return;
                     fields->items[index] = dfe.GetValue();
                     page->UpdateValues();
                   });
  }

  {
    GroupedListWidget::ItemOptions options;
    options.value_callback =
      [fields](GroupedListWidget::ValueState &state) {
        state.hidden = !fields->custom_menu;
        state.text.clear();
      };
    list->AddItem(_("Add command"), [fields, page] {
      if (!fields->custom_menu)
        return;
      if (fields->visible_count >= UISettings::MAX_CUSTOM_QUICK_MENU)
        return;
      fields->items[fields->visible_count] = 0;
      ++fields->visible_count;
      if (page->UpdateValues())
        page->UpdateLayout();
    }, options);
  }

  list->SetSaveCallback([fields](bool &changed) {
    UISettings &settings = CommonInterface::SetUISettings();

    ConfigPanel::CommitSetting(changed, settings.custom_quick_menu,
                               fields->custom_menu,
                               ProfileKeys::CustomQuickMenu);

    /* Always persist the command list, even when Custom menu is off, so
       the selection returns when the user enables it again. */
    unsigned new_count = 0;
    uint8_t new_items[UISettings::MAX_CUSTOM_QUICK_MENU]{};

    for (unsigned i = 0; i < fields->visible_count; ++i) {
      const unsigned location = fields->items[i];
      if (location == 0 || location >= Menu::MAX_ITEMS)
        continue;

      new_items[new_count++] = (uint8_t)location;
    }

    bool list_changed = new_count != settings.custom_quick_menu_count;
    if (!list_changed) {
      for (unsigned i = 0; i < new_count; ++i) {
        if (new_items[i] != settings.custom_quick_menu_items[i]) {
          list_changed = true;
          break;
        }
      }
    }

    if (list_changed) {
      changed = true;
      settings.custom_quick_menu_count = new_count;
      for (unsigned i = 0; i < UISettings::MAX_CUSTOM_QUICK_MENU; ++i)
        settings.custom_quick_menu_items[i] =
          i < new_count ? new_items[i] : 0;

      Profile::Set(ProfileKeys::CustomQuickMenuCount, new_count);
      for (unsigned i = 0; i < UISettings::MAX_CUSTOM_QUICK_MENU; ++i) {
        char profile_key[32];
        StringFormat(profile_key, sizeof(profile_key),
                     "CustomQuickMenuItem%u", i);
        if (i < new_count)
          Profile::Set(profile_key, (unsigned)new_items[i]);
        else
          Profile::Set(profile_key, 0u);
      }
    }

    return true;
  });

  return list;
}
