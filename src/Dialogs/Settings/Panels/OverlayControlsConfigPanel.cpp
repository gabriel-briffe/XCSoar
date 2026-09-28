// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "OverlayControlsConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MainWindow.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UISettings.hpp"

#include <cstdint>

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

/** The buttons and the touch marks drawn over the map. */
class OverlayControlsConfigPanel final : public ConfigListPanel {
  bool show_menu_button;
  bool show_zoom_button;
  QuickMenuButtonMode quick_menu_button;
  unsigned touch_areas_transparency;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
OverlayControlsConfigPanel::LoadSettings() noexcept
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  show_menu_button = ui_settings.show_menu_button;
  show_zoom_button = ui_settings.show_zoom_button;
  quick_menu_button = QuickMenuButtonModeFromSettings(ui_settings);
  touch_areas_transparency = ui_settings.touch_areas_transparency;
}

void
OverlayControlsConfigPanel::Fill() noexcept
{
  if (!IsExpert())
    return;

  AddGroup();

  AddToggleItem(_("Show Menu button"), _("Show the Menu button"),
                show_menu_button);
  AddToggleItem(_("Show Zoom button"), _("Show the Zoom button"),
                show_zoom_button);
  AddEnumItem(C_("Setting", "Show QuickMenu button"),
              _("Show the QuickMenu button on the map, hide it, or keep an "
                "invisible touch target."),
              quick_menu_button_mode_list, quick_menu_button);
  AddEnumItem(C_("Setting", "Touch areas transparency"),
              _("Pink markers for invisible map touch targets (QuickMenu when "
                "transparent, compass, airspace, pan, bottom area).  0 % is "
                "solid pink; 100 % hides the markers."),
              touch_areas_transparency_list, touch_areas_transparency);
}

bool
OverlayControlsConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  UISettings &ui_settings = CommonInterface::SetUISettings();
  bool overlay_buttons_changed = false;

  if (Profile::Update(ProfileKeys::ShowMenuButton,
                      ui_settings.show_menu_button, show_menu_button)) {
    changed = true;
    overlay_buttons_changed = true;
  }
  if (Profile::Update(ProfileKeys::ShowZoomButton,
                      ui_settings.show_zoom_button, show_zoom_button)) {
    changed = true;
    overlay_buttons_changed = true;
  }

  {
    const bool old_show = ui_settings.show_quickmenu_button;
    const bool old_transparent = ui_settings.transparent_quickmenu_button;
    ApplyQuickMenuButtonMode(ui_settings, quick_menu_button);
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

  if (Profile::Update(ProfileKeys::TouchAreasTransparency,
                      ui_settings.touch_areas_transparency,
                      touch_areas_transparency)) {
    changed = true;
    CommonInterface::main_window->InvalidateMapOverlayButtons();
  }

  if (overlay_buttons_changed)
    CommonInterface::main_window->ReinitialiseMapOverlayButtons();

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateOverlayControlsConfigPanel()
{
  return std::make_unique<OverlayControlsConfigPanel>();
}
