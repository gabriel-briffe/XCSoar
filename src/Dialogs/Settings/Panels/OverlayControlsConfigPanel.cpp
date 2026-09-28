// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "OverlayControlsConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MainWindow.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "UISettings.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <cstdint>
#include <memory>

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

std::unique_ptr<Widget>
CreateOverlayControlsConfigPanel()
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  struct Fields {
    bool show_menu_button;
    bool show_zoom_button;
    QuickMenuButtonMode quick_menu_button;
    unsigned touch_areas_transparency;
  };

  auto fields = std::make_shared<Fields>(Fields{
    ui_settings.show_menu_button,
    ui_settings.show_zoom_button,
    QuickMenuButtonModeFromSettings(ui_settings),
    ui_settings.touch_areas_transparency,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddSwitch(_("Show Menu button"), _("Show the Menu button"),
                  fields->show_menu_button, true);
  list->AddSwitch(_("Show Zoom button"), _("Show the Zoom button"),
                  fields->show_zoom_button, true);
  list->AddEnum(C_("Setting", "Show QuickMenu button"),
                _("Show the QuickMenu button on the map, hide it, or keep an "
                  "invisible touch target."),
                quick_menu_button_mode_list,
                fields->quick_menu_button, true);
  list->AddEnum(C_("Setting", "Touch areas transparency"),
                _("Pink markers for invisible map touch targets (QuickMenu when "
                  "transparent, compass, airspace, pan, bottom area).  0 % is "
                  "solid pink; 100 % hides the markers."),
                touch_areas_transparency_list,
                fields->touch_areas_transparency, true);

  list->SetSaveCallback([fields](bool &changed) {
    UISettings &ui_settings = CommonInterface::SetUISettings();

    bool overlay_buttons_changed = false;

    if (ConfigPanel::CommitSetting(changed, ui_settings.show_menu_button,
                                   fields->show_menu_button,
                                   ProfileKeys::ShowMenuButton))
      overlay_buttons_changed = true;
    if (ConfigPanel::CommitSetting(changed, ui_settings.show_zoom_button,
                                   fields->show_zoom_button,
                                   ProfileKeys::ShowZoomButton))
      overlay_buttons_changed = true;

    {
      const bool old_show = ui_settings.show_quickmenu_button;
      const bool old_transparent = ui_settings.transparent_quickmenu_button;
      ApplyQuickMenuButtonMode(ui_settings, fields->quick_menu_button);
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

    if (ConfigPanel::CommitSetting(changed,
                                   ui_settings.touch_areas_transparency,
                                   fields->touch_areas_transparency,
                                   ProfileKeys::TouchAreasTransparency))
      CommonInterface::main_window->InvalidateMapOverlayButtons();

    if (overlay_buttons_changed)
      CommonInterface::main_window->ReinitialiseMapOverlayButtons();

    return true;
  });

  return list;
}
