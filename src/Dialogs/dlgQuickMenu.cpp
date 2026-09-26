// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Dialogs/Dialogs.h"
#include "Dialogs/dlgConfigMenu.hpp"
#include "Input/InputEvents.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Menu/MenuData.hpp"
#include "UISettings.hpp"
#include "ui/window/SingleWindow.hpp"

void
dlgQuickMenuShowModal(UI::SingleWindow &parent) noexcept
{
  const auto *menu = InputEvents::GetMenu("RemoteStick");
  if (menu == nullptr)
    return;

  const UISettings &ui = CommonInterface::GetUISettings();
  Menu filtered_menu;
  const Menu *show_menu = menu;

  if (ui.custom_quick_menu && ui.custom_quick_menu_count > 0) {
    filtered_menu.Clear();
    unsigned dest = 0;
    for (unsigned i = 0; i < ui.custom_quick_menu_count; ++i) {
      const unsigned location = ui.custom_quick_menu_items[i];
      if (location >= Menu::MAX_ITEMS)
        continue;

      const auto &item = (*menu)[location];
      if (!item.IsDefined())
        continue;

      filtered_menu.Add(item.label, dest++, item.event);
    }

    if (dest > 0)
      show_menu = &filtered_menu;
  }

  ShowTiledMenuFromMenu(parent, N_("Quick Menu"), "QuickMenu", *show_menu);
}
