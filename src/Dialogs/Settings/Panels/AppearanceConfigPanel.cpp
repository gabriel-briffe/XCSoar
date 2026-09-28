// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AppearanceConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "UISettings.hpp"
#include "UtilsSettings.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

#ifdef ANDROID
#include "Android/Main.hpp"
#include "Android/NativeView.hpp"
#endif

static constexpr StaticEnumChoice tabdialog_style_list[] = {
  { DialogSettings::TabStyle::Text, N_("Text"),
    N_("Show text on tabbed dialogs.") },
  { DialogSettings::TabStyle::Icon, N_("Icons"),
    N_("Show icons on tabbed dialogs.")},
  nullptr
};

static constexpr StaticEnumChoice popup_msg_position_list[] = {
  { UISettings::PopupMessagePosition::CENTER, N_("Center"),
    N_("Center the status message boxes.") },
  { UISettings::PopupMessagePosition::TOP_LEFT, N_("Top left"),
    N_("Show status message boxes in the top left corner.") },
  nullptr
};

#ifndef KOBO
static constexpr StaticEnumChoice dark_mode_list[] = {
  { UISettings::DarkMode::AUTO, NC_("Setting", "Auto"),
    N_("Use the system-wide setting") },
  { UISettings::DarkMode::OFF, N_("Off"),
    N_("Black text on white background") },
  { UISettings::DarkMode::ON, N_("On"),
    N_("White text on black background") },
  nullptr
};
#endif

std::unique_ptr<Widget>
CreateAppearanceConfigPanel()
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  struct Fields {
#ifdef ANDROID
    bool full_screen;
#endif
    int scale;
#ifndef KOBO
    UISettings::DarkMode dark_mode;
#endif
    DialogSettings::TabStyle tab_style;
    UISettings::PopupMessagePosition popup_message_position;
    bool tiled_menu;
  };

  auto fields = std::make_shared<Fields>(Fields{
#ifdef ANDROID
    ui_settings.display.full_screen,
#endif
    int(ui_settings.scale),
#ifndef KOBO
    ui_settings.dark_mode,
#endif
    ui_settings.dialog.tab_style,
    ui_settings.popup_message_position,
    ui_settings.dialog.tiled_menu,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

#ifdef ANDROID
  list->AddSwitch(_("Full screen"), _("Run XCSoar in full screen mode"),
                  fields->full_screen);
#endif

  list->AddInteger(_("Text size"),
                   nullptr,
                   "%d %%", "%d",
                   UISettings::SCALE_MIN, UISettings::SCALE_MAX,
                   UISettings::SCALE_STEP,
                   fields->scale);

#ifndef KOBO
  list->AddEnum(_("Dark mode"), nullptr, dark_mode_list,
                fields->dark_mode, true);
#endif

  list->AddEnum(_("Tab dialog style"), nullptr,
                tabdialog_style_list, fields->tab_style);

  list->AddEnum(_("Message display"), nullptr,
                popup_msg_position_list,
                fields->popup_message_position, true);

  list->AddSwitch(_("Tiled menu"),
                  _("Show Configuration as a tile grid instead of the "
                    "two-column list."),
                  fields->tiled_menu);

  list->SetSaveCallback([fields](bool &changed) {
    UISettings &ui_settings = CommonInterface::SetUISettings();

#ifdef ANDROID
    if (ConfigPanel::CommitSetting(changed, ui_settings.display.full_screen,
                                   fields->full_screen,
                                   ProfileKeys::FullScreen))
      native_view->SetFullScreen(Java::GetEnv(),
                                 ui_settings.display.full_screen);
#endif

    unsigned scale = unsigned(fields->scale);
    if (ConfigPanel::CommitSetting(changed, ui_settings.scale, scale,
                                   ProfileKeys::UIScale))
      require_restart = true;

#ifndef KOBO
    ConfigPanel::CommitSetting(changed, ui_settings.dark_mode,
                               fields->dark_mode, ProfileKeys::DarkMode);
#else
    if (ui_settings.dark_mode != UISettings::DarkMode::OFF) {
      ui_settings.dark_mode = UISettings::DarkMode::OFF;
      changed = true;
    }
#endif

    ConfigPanel::CommitSetting(changed, ui_settings.popup_message_position,
                               fields->popup_message_position,
                               ProfileKeys::AppStatusMessageAlignment);

    DialogSettings &dialog_settings = ui_settings.dialog;
    ConfigPanel::CommitSetting(changed, dialog_settings.tab_style,
                               fields->tab_style,
                               ProfileKeys::AppDialogTabStyle);
    ConfigPanel::CommitSetting(changed, dialog_settings.tiled_menu,
                               fields->tiled_menu,
                               ProfileKeys::AppDialogTiledMenu);

    return true;
  });

  return list;
}
