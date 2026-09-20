// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AppearanceConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "UISettings.hpp"
#include "UtilsSettings.hpp"

#ifdef ANDROID
#include "Android/Main.hpp"
#include "Android/NativeView.hpp"
#endif

enum ControlIndex {
#ifdef ANDROID
  FullScreen,
#endif
  UIScale,
  DarkMode,
  TabDialogStyle,
  AppStatusMessageAlignment,
};

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

static constexpr StaticEnumChoice dark_mode_list[] = {
  { UISettings::DarkMode::AUTO, NC_("Setting", "Auto"),
    N_("Use the system-wide setting") },
  { UISettings::DarkMode::OFF, N_("Off"),
    N_("Black text on white background") },
  { UISettings::DarkMode::ON, N_("On"),
    N_("White text on black background") },
  nullptr
};

class AppearanceConfigPanel final : public RowFormWidget {
public:
  AppearanceConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
AppearanceConfigPanel::Prepare(ContainerWindow &parent,
                               const PixelRect &rc) noexcept
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  RowFormWidget::Prepare(parent, rc);

#ifdef ANDROID
  AddBoolean(_("Full screen"), _("Run XCSoar in full screen mode"),
             ui_settings.display.full_screen);
#endif

  AddInteger(_("Text size"),
             nullptr,
             "%d %%", "%d", 75, 200, 5,
             ui_settings.scale);

#ifndef KOBO
  AddEnum(_("Dark mode"), nullptr, dark_mode_list,
          (unsigned)ui_settings.dark_mode);
  SetExpertRow(DarkMode);
#else
  AddDummy();
#endif

  AddEnum(_("Tab dialog style"), nullptr,
          tabdialog_style_list, (unsigned)ui_settings.dialog.tab_style);

  AddEnum(_("Message display"), nullptr,
          popup_msg_position_list,
          (unsigned)ui_settings.popup_message_position);
  SetExpertRow(AppStatusMessageAlignment);
}

bool
AppearanceConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  UISettings &ui_settings = CommonInterface::SetUISettings();

#ifdef ANDROID
  changed |= SaveValue(FullScreen, ProfileKeys::FullScreen,
                       ui_settings.display.full_screen);
  native_view->SetFullScreen(Java::GetEnv(), ui_settings.display.full_screen);
#endif

  if (SaveValueInteger(UIScale, ProfileKeys::UIScale, ui_settings.scale))
    require_restart = changed = true;

#ifndef KOBO
  changed |= SaveValueEnum(DarkMode, ProfileKeys::DarkMode,
                           ui_settings.dark_mode);
#else
  if (ui_settings.dark_mode != UISettings::DarkMode::OFF) {
    ui_settings.dark_mode = UISettings::DarkMode::OFF;
    changed = true;
  }
#endif

  changed |= SaveValueEnum(AppStatusMessageAlignment,
                           ProfileKeys::AppStatusMessageAlignment,
                           ui_settings.popup_message_position);

  DialogSettings &dialog_settings = CommonInterface::SetUISettings().dialog;
  changed |= SaveValueEnum(TabDialogStyle, ProfileKeys::AppDialogTabStyle,
                           dialog_settings.tab_style);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateAppearanceConfigPanel()
{
  return std::make_unique<AppearanceConfigPanel>();
}
