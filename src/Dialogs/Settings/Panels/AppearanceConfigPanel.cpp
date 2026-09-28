// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AppearanceConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UISettings.hpp"
#include "UtilsSettings.hpp"

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

/** How the screens and the dialogs look. */
class AppearanceConfigPanel final : public ConfigListPanel {
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

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
AppearanceConfigPanel::LoadSettings() noexcept
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

#ifdef ANDROID
  full_screen = ui_settings.display.full_screen;
#endif
  scale = int(ui_settings.scale);
#ifndef KOBO
  dark_mode = ui_settings.dark_mode;
#endif
  tab_style = ui_settings.dialog.tab_style;
  popup_message_position = ui_settings.popup_message_position;
  tiled_menu = ui_settings.dialog.tiled_menu;
}

void
AppearanceConfigPanel::Fill() noexcept
{
  AddGroup();

#ifdef ANDROID
  AddToggleItem(_("Full screen"), _("Run XCSoar in full screen mode"),
                full_screen);
#endif

  AddPercentItem(_("Text size"), nullptr,
                 UISettings::SCALE_MIN, UISettings::SCALE_MAX,
                 UISettings::SCALE_STEP, scale);

#ifndef KOBO
  if (IsExpert())
    AddEnumItem(_("Dark mode"), nullptr, dark_mode_list, dark_mode);
#endif

  AddEnumItem(_("Tab dialog style"), nullptr,
              tabdialog_style_list, tab_style);

  if (IsExpert())
    AddEnumItem(_("Message display"), nullptr,
                popup_msg_position_list, popup_message_position);

  AddToggleItem(_("Tiled menu"),
                _("Show Configuration as a tile grid instead of the "
                  "two-column list."),
                tiled_menu);
}

bool
AppearanceConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  UISettings &ui_settings = CommonInterface::SetUISettings();

#ifdef ANDROID
  if (Profile::Update(ProfileKeys::FullScreen,
                      ui_settings.display.full_screen, full_screen)) {
    changed = true;
    native_view->SetFullScreen(Java::GetEnv(),
                               ui_settings.display.full_screen);
  }
#endif

  if (Profile::Update(ProfileKeys::UIScale, ui_settings.scale,
                      static_cast<unsigned>(scale))) {
    changed = true;
    require_restart = true;
  }

#ifndef KOBO
  changed |= Profile::Update(ProfileKeys::DarkMode, ui_settings.dark_mode,
                             dark_mode);
#else
  if (ui_settings.dark_mode != UISettings::DarkMode::OFF) {
    ui_settings.dark_mode = UISettings::DarkMode::OFF;
    changed = true;
  }
#endif

  changed |= Profile::Update(ProfileKeys::AppStatusMessageAlignment,
                             ui_settings.popup_message_position,
                             popup_message_position);

  DialogSettings &dialog_settings = ui_settings.dialog;
  changed |= Profile::Update(ProfileKeys::AppDialogTabStyle,
                             dialog_settings.tab_style, tab_style);
  changed |= Profile::Update(ProfileKeys::AppDialogTiledMenu,
                             dialog_settings.tiled_menu, tiled_menu);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateAppearanceConfigPanel()
{
  return std::make_unique<AppearanceConfigPanel>();
}
