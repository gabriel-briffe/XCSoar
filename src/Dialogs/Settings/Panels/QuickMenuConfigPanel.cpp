// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "QuickMenuConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Input/InputEvents.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Menu/ButtonLabel.hpp"
#include "Menu/MenuData.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UISettings.hpp"
#include "util/StaticString.hxx"
#include "util/StringFormat.hpp"
#include "util/TruncateString.hpp"
#include "util/UTF8.hpp"

#include <cstddef>
#include <vector>

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

/**
 * The commands the Quick Menu shows when a custom list is on.
 */
class QuickMenuConfigPanel final : public ConfigListPanel {
  bool custom_menu;
  unsigned visible_count;
  unsigned items[UISettings::MAX_CUSTOM_QUICK_MENU];
  StaticString<8> captions[UISettings::MAX_CUSTOM_QUICK_MENU];

  void SlotLabel(unsigned index, StaticString<128> &dest) const noexcept;
  void PickSlot(unsigned index) noexcept;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
QuickMenuConfigPanel::SlotLabel(unsigned index,
                                StaticString<128> &dest) const noexcept
{
  DataFieldEnum dfe;
  FillQuickMenuChoices(dfe);
  dfe.SetValue(items[index]);
  dest = dfe.GetAsDisplayString();
}

void
QuickMenuConfigPanel::PickSlot(unsigned index) noexcept
{
  DataFieldEnum dfe;
  FillQuickMenuChoices(dfe);
  dfe.SetValue(items[index]);

  std::vector<StaticString<128>> labels(dfe.Count());
  std::vector<PickerChoice> choices;
  choices.reserve(dfe.Count());
  int current = 0;

  for (std::size_t i = 0; i < dfe.Count(); ++i) {
    labels[i] = dfe[i].GetDisplayString();
    choices.push_back({labels[i].c_str()});
    if (dfe[i].GetId() == dfe.GetValue())
      current = int(i);
  }

  const int picked = PickChoice(captions[index].c_str(), nullptr,
                                choices, current);
  if (picked < 0)
    return;

  items[index] = dfe[picked].GetId();
  Refresh();
}

void
QuickMenuConfigPanel::LoadSettings() noexcept
{
  const UISettings &settings = CommonInterface::GetUISettings();

  custom_menu = settings.custom_quick_menu;
  visible_count = settings.custom_quick_menu_count > 0
    ? settings.custom_quick_menu_count
    : 1;

  for (unsigned i = 0; i < UISettings::MAX_CUSTOM_QUICK_MENU; ++i) {
    items[i] = i < settings.custom_quick_menu_count
      ? settings.custom_quick_menu_items[i]
      : 0;
    captions[i].Format("%u", i + 1);
  }
}

void
QuickMenuConfigPanel::Fill() noexcept
{
  AddGroup();

  AddToggleItem(_("Custom menu"),
                _("When enabled, the Quick Menu shows only the commands "
                  "selected below, in that order. When disabled, the full "
                  "default Quick Menu is used; your selection is kept for "
                  "when you turn this back on."),
                custom_menu);

  if (!custom_menu)
    return;

  for (unsigned i = 0; i < visible_count; ++i) {
    StaticString<128> label;
    SlotLabel(i, label);
    AddItem(captions[i].c_str(), [this, i](){ PickSlot(i); },
            {.value = label.c_str(), .chevron = true});
  }

  AddItem(_("Add command"), [this](){
    if (visible_count >= UISettings::MAX_CUSTOM_QUICK_MENU)
      return;

    items[visible_count] = 0;
    ++visible_count;
    Refresh();
  });
}

bool
QuickMenuConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  UISettings &settings = CommonInterface::SetUISettings();

  changed |= Profile::Update(ProfileKeys::CustomQuickMenu,
                             settings.custom_quick_menu, custom_menu);

  /* Always persist the command list, even when Custom menu is off, so
     the selection returns when the user enables it again. */
  unsigned new_count = 0;
  uint8_t new_items[UISettings::MAX_CUSTOM_QUICK_MENU]{};

  for (unsigned i = 0; i < visible_count; ++i) {
    const unsigned location = items[i];
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

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateQuickMenuConfigPanel()
{
  return std::make_unique<QuickMenuConfigPanel>();
}
