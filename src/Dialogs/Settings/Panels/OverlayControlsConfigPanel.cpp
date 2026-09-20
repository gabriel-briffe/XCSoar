// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "OverlayControlsConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "MainWindow.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "UISettings.hpp"

#include <cstdint>

enum ControlIndex {
  ShowMenuButton,
  ShowZoomButton,
  ShowQuickMenuButton,
  TouchAreasTransparency,
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

class OverlayControlsConfigPanel final : public RowFormWidget {
public:
  OverlayControlsConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
OverlayControlsConfigPanel::Prepare(ContainerWindow &parent,
                                    const PixelRect &rc) noexcept
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  RowFormWidget::Prepare(parent, rc);

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
}

bool
OverlayControlsConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  UISettings &ui_settings = CommonInterface::SetUISettings();

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

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateOverlayControlsConfigPanel()
{
  return std::make_unique<OverlayControlsConfigPanel>();
}
