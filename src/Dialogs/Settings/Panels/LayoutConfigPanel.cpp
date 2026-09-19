// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "LayoutConfigPanel.hpp"
#include "ui/canvas/Features.hpp" // for DRAW_MOUSE_CURSOR
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Form/DataField/Enum.hpp"
#include "Hardware/RotateDisplay.hpp"
#include "Interface.hpp"
#include "MainWindow.hpp"
#include "LogFile.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "Menu/ShowButton.hpp"
#include "ActionInterface.hpp"
#include "util/Macros.hpp"
#include "UISettings.hpp"

#include <cstdint>

#ifdef ANDROID
#include "Android/Main.hpp"
#include "Android/NativeView.hpp"
#endif

#ifdef USE_POLL_EVENT
#include "ui/event/Globals.hpp"
#include "ui/event/Queue.hpp"
#endif

enum ControlIndex {
#ifdef ANDROID
  FullScreen,
#endif
  MapOrientation,
  DarkMode,
  AppDisplayType,
  TabDialogStyle,
  AppStatusMessageAlignment,
  ShowMenuButton,
  ShowZoomButton,
  ShowQuickMenuButton,
  TouchAreasTransparency,
#ifdef DRAW_MOUSE_CURSOR
  CursorSize,
  CursorInverted,
#endif
};

enum class QuickMenuButtonMode : uint8_t {
  OFF,
  ON,
  TRANSPARENT,
};

static constexpr StaticEnumChoice quick_menu_button_mode_list[] = {
  { QuickMenuButtonMode::OFF, N_("Off"),
    N_("Do not show the QuickMenu button on the map.") },
  { QuickMenuButtonMode::ON, N_("On"),
    N_("Show the QuickMenu button on the map.") },
  { QuickMenuButtonMode::TRANSPARENT, N_("Transparent"),
    N_("Keep an invisible touch target for the QuickMenu.") },
  nullptr
};

static constexpr StaticEnumChoice touch_areas_transparency_list[] = {
  { 0, "0 %" },
  { 10, "10 %" },
  { 20, "20 %" },
  { 30, "30 %" },
  { 40, "40 %" },
  { 50, "50 %" },
  { 60, "60 %" },
  { 70, "70 %" },
  { 80, "80 %" },
  { 90, "90 %" },
  { 100, "100 %" },
  nullptr
};

static constexpr QuickMenuButtonMode
QuickMenuButtonModeFromSettings(const UISettings &settings) noexcept
{
  if (!settings.show_quickmenu_button)
    return QuickMenuButtonMode::OFF;
  if (settings.transparent_quickmenu_button)
    return QuickMenuButtonMode::TRANSPARENT;
  return QuickMenuButtonMode::ON;
}

static void
ApplyQuickMenuButtonMode(UISettings &settings,
                         QuickMenuButtonMode mode) noexcept
{
  switch (mode) {
  case QuickMenuButtonMode::OFF:
    settings.show_quickmenu_button = false;
    settings.transparent_quickmenu_button = false;
    break;
  case QuickMenuButtonMode::ON:
    settings.show_quickmenu_button = true;
    settings.transparent_quickmenu_button = false;
    break;
  case QuickMenuButtonMode::TRANSPARENT:
    settings.show_quickmenu_button = true;
    settings.transparent_quickmenu_button = true;
    break;
  }
}

static constexpr StaticEnumChoice display_orientation_list[] = {
  { DisplayOrientation::DEFAULT,
    N_("Default") },
  { DisplayOrientation::PORTRAIT,
    N_("Portrait") },
  { DisplayOrientation::LANDSCAPE,
    N_("Landscape") },
  { DisplayOrientation::REVERSE_PORTRAIT,
    N_("Reverse Portrait") },
  { DisplayOrientation::REVERSE_LANDSCAPE,
    N_("Reverse Landscape") },
  nullptr
};

static constexpr StaticEnumChoice display_type_list[] = {
  { DisplayType::LCD, NC_("Setting", "LCD"),
    N_("Conventional LCD or OLED. Full scrolling animations.") },
  { DisplayType::E_INK, NC_("Setting", "E-ink"),
    N_("Monochrome electronic paper. Disables kinetic and smooth "
       "scrolling.") },
  { DisplayType::COLOR_E_INK, NC_("Setting", "Color e-ink"),
    N_("Color electronic paper. Disables kinetic and smooth "
       "scrolling like monochrome e-ink.") },
  nullptr
};

static_assert(ARRAY_SIZE(display_type_list) ==
              unsigned(DisplayType::COUNT) + 1,
              "display_type_list must match DisplayType::COUNT");

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

class LayoutConfigPanel final : public RowFormWidget {
public:
  LayoutConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
LayoutConfigPanel::Prepare(ContainerWindow &parent,
                           const PixelRect &rc) noexcept
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  RowFormWidget::Prepare(parent, rc);

#ifdef ANDROID
  AddBoolean(_("Full screen"), _("Run XCSoar in full screen mode"),
             ui_settings.display.full_screen);
#endif

  if (Display::RotateSupported())
    AddEnum(_("Display orientation"), _("Rotate the display on devices that support it."),
            display_orientation_list, (unsigned)ui_settings.display.orientation);
  else
    AddDummy();

#ifndef KOBO
  AddEnum(_("Dark mode"), nullptr, dark_mode_list,
          (unsigned)ui_settings.dark_mode);
  SetExpertRow(DarkMode);
#else
  AddDummy();
#endif

  AddEnum(C_("Setting", "Display type"),
          _("Select the display technology. E-ink modes disable kinetic "
            "and smooth scrolling for slow refresh screens."),
          display_type_list,
          (unsigned)ui_settings.display.display_type);
  SetExpertRow(AppDisplayType);

  AddEnum(_("Tab dialog style"), nullptr,
          tabdialog_style_list, (unsigned)ui_settings.dialog.tab_style);

  AddEnum(_("Message display"), nullptr,
          popup_msg_position_list,
          (unsigned)ui_settings.popup_message_position);
  SetExpertRow(AppStatusMessageAlignment);

  AddBoolean(_("Show Menu button"), _("Show the Menu button"),
             ui_settings.show_menu_button);
  SetExpertRow(ShowMenuButton);
  AddBoolean(_("Show Zoom button"), _("Show the Zoom button"),
             ui_settings.show_zoom_button);
  SetExpertRow(ShowZoomButton);
  AddEnum(C_("Setting", "Show QuickMenu button"),
          _("Show the QuickMenu button on the map, hide it, or keep an "
            "invisible touch target."),
          quick_menu_button_mode_list,
          (unsigned)QuickMenuButtonModeFromSettings(ui_settings));
  SetExpertRow(ShowQuickMenuButton);
  AddEnum(C_("Setting", "Touch areas transparency"),
          _("Pink markers for invisible map touch targets (QuickMenu when "
            "transparent, compass, airspace, pan, bottom area).  0 % is "
            "solid pink; 100 % hides the markers."),
          touch_areas_transparency_list,
          ui_settings.touch_areas_transparency);
  SetExpertRow(TouchAreasTransparency);

#ifdef DRAW_MOUSE_CURSOR
  AddInteger(_("Cursor zoom"), _("Cursor zoom factor"), "%d x", "%d x", 1, 10, 1,
             (unsigned)ui_settings.display.cursor_size);
  AddBoolean(_("Invert cursor color"), _("Enable black cursor"),
             ui_settings.display.invert_cursor_colors);
#endif
}

bool
LayoutConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;

  UISettings &ui_settings = CommonInterface::SetUISettings();

#ifdef ANDROID
  changed |= SaveValue(FullScreen, ProfileKeys::FullScreen,
                       ui_settings.display.full_screen);
  native_view->SetFullScreen(Java::GetEnv(), ui_settings.display.full_screen);
#endif

  bool orientation_changed = false;

  if (Display::RotateSupported()) {
    orientation_changed =
      SaveValueEnum(MapOrientation, ProfileKeys::MapOrientation,
                    ui_settings.display.orientation);
    changed |= orientation_changed;
  }

#ifndef KOBO
  changed |= SaveValueEnum(DarkMode, ProfileKeys::DarkMode,
                           ui_settings.dark_mode);
#else
  if (ui_settings.dark_mode != UISettings::DarkMode::OFF) {
    ui_settings.dark_mode = UISettings::DarkMode::OFF;
    changed = true;
  }
#endif

  if (SaveValueEnum(AppDisplayType, ProfileKeys::DisplayType,
                    ui_settings.display.display_type)) {
    changed = true;
    SetDisplayType(ui_settings.display.display_type);
  }

  changed |= SaveValueEnum(AppStatusMessageAlignment, ProfileKeys::AppStatusMessageAlignment,
                           ui_settings.popup_message_position);

  bool overlay_buttons_changed = false;
  if (SaveValue(ShowMenuButton, ProfileKeys::ShowMenuButton,
                ui_settings.show_menu_button))
    overlay_buttons_changed = changed = true;
  if (SaveValue(ShowZoomButton, ProfileKeys::ShowZoomButton,
                ui_settings.show_zoom_button))
    overlay_buttons_changed = changed = true;

  {
    const auto mode = (QuickMenuButtonMode)GetValueEnum(ShowQuickMenuButton);
    const bool old_show = ui_settings.show_quickmenu_button;
    const bool old_transparent = ui_settings.transparent_quickmenu_button;
    ApplyQuickMenuButtonMode(ui_settings, mode);
    if (ui_settings.show_quickmenu_button != old_show ||
        ui_settings.transparent_quickmenu_button != old_transparent) {
      Profile::Set(ProfileKeys::ShowQuickMenuButton,
                   ui_settings.show_quickmenu_button);
      Profile::Set(ProfileKeys::TransparentQuickMenuButton,
                   ui_settings.transparent_quickmenu_button);
      overlay_buttons_changed = changed = true;
      CommonInterface::main_window->InvalidateMapOverlayButtons();
    }
  }

  if (SaveValueEnum(TouchAreasTransparency,
                    ProfileKeys::TouchAreasTransparency,
                    ui_settings.touch_areas_transparency)) {
    changed = true;
    CommonInterface::main_window->InvalidateMapOverlayButtons();
  }

  if (overlay_buttons_changed)
    CommonInterface::main_window->ReinitialiseMapOverlayButtons();

  DialogSettings &dialog_settings = CommonInterface::SetUISettings().dialog;
  changed |= SaveValueEnum(TabDialogStyle, ProfileKeys::AppDialogTabStyle, dialog_settings.tab_style);

#ifdef DRAW_MOUSE_CURSOR
  changed |= SaveValueInteger(CursorSize, ProfileKeys::CursorSize,
                              ui_settings.display.cursor_size);
  CommonInterface::main_window->SetCursorSize(ui_settings.display.cursor_size);

  changed |= SaveValue(CursorInverted, ProfileKeys::CursorColorsInverted, ui_settings.display.invert_cursor_colors);
  CommonInterface::main_window->SetCursorColorsInverted(ui_settings.display.invert_cursor_colors);
#endif

  if (orientation_changed) {
    assert(Display::RotateSupported());

    if (!Display::Rotate(ui_settings.display.orientation))
      LogString("Display rotation failed");

#ifdef USE_POLL_EVENT
    UI::event_queue->SetDisplayOrientation(ui_settings.display.orientation);
#endif

    CommonInterface::main_window->CheckResize();
  }

  _changed |= changed;

  return true;
}

std::unique_ptr<Widget>
CreateLayoutConfigPanel()
{
  return std::make_unique<LayoutConfigPanel>();
}
