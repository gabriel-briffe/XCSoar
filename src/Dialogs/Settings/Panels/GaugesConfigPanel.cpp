// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GaugesConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MainWindow.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

static constexpr StaticEnumChoice final_glide_bar_display_mode_list[] = {
  { FinalGlideBarDisplayMode::OFF, N_("Off"),
    N_("Disable final glide bar.") },
  { FinalGlideBarDisplayMode::ON, N_("On"),
    N_("Always show final glide bar.") },
  { FinalGlideBarDisplayMode::AUTO, NC_("Setting", "Auto"),
    N_("Show final glide bar if approaching final glide range.") },
  nullptr
};

static constexpr StaticEnumChoice flarm_display_location_list[] = {
  { TrafficSettings::GaugeLocation::AUTO,
    N_("Auto (follow InfoBoxes)") },
  { TrafficSettings::GaugeLocation::TOP_LEFT,
    N_("Top left") },
  { TrafficSettings::GaugeLocation::TOP_RIGHT,
    N_("Top right") },
  { TrafficSettings::GaugeLocation::BOTTOM_LEFT,
    N_("Bottom left") },
  { TrafficSettings::GaugeLocation::BOTTOM_RIGHT,
    N_("Bottom right") },
  { TrafficSettings::GaugeLocation::CENTER_TOP,
    N_("Center top") },
  { TrafficSettings::GaugeLocation::CENTER_BOTTOM,
    N_("Center bottom") },
  { TrafficSettings::GaugeLocation::TOP_LEFT_AVOID_IB,
    N_("Top left (avoid InfoBoxes)") },
  { TrafficSettings::GaugeLocation::TOP_RIGHT_AVOID_IB,
    N_("Top right (avoid InfoBoxes)") },
  { TrafficSettings::GaugeLocation::BOTTOM_LEFT_AVOID_IB,
    N_("Bottom left (avoid InfoBoxes)") },
  { TrafficSettings::GaugeLocation::BOTTOM_RIGHT_AVOID_IB,
    N_("Bottom right (avoid InfoBoxes)") },
  { TrafficSettings::GaugeLocation::CENTER_TOP_AVOID_IB,
    N_("Center top (avoid InfoBoxes)") },
  { TrafficSettings::GaugeLocation::CENTER_BOTTOM_AVOID_IB,
    N_("Center bottom (avoid InfoBoxes)") },
  nullptr
};

static constexpr StaticEnumChoice thermal_assistant_position_list[] = {
  { UISettings::ThermalAssistantPosition::OFF,
    N_("Off"),
    N_("Disable thermal assistant.") },
  { UISettings::ThermalAssistantPosition::BOTTOM_LEFT,
    N_("Bottom left"),
    N_("Show thermal assistant in bottom left.") },
  { UISettings::ThermalAssistantPosition::BOTTOM_LEFT_AVOID_IB,
    N_("Bottom left (avoid InfoBoxes)"),
    N_("Show thermal assistant in bottom left, above or to the right of InfoBoxes (if present).") },
  { UISettings::ThermalAssistantPosition::BOTTOM_RIGHT,
    N_("Bottom right"),
    N_("Show thermal assistant in bottom right.") },
  { UISettings::ThermalAssistantPosition::BOTTOM_RIGHT_AVOID_IB,
    N_("Bottom right (avoid InfoBoxes)"),
    N_("Show thermal assistant in bottom right, above or to the left of InfoBoxes (if present).") },
  { UISettings::ThermalAssistantPosition::TOP_LEFT,
    N_("Top left"),
    N_("Show thermal assistant in top left.") },
  { UISettings::ThermalAssistantPosition::TOP_RIGHT,
    N_("Top right"),
    N_("Show thermal assistant in top right.") },
  { UISettings::ThermalAssistantPosition::CENTER_TOP,
    N_("Center top"),
    N_("Show thermal assistant in center top.") },
  { UISettings::ThermalAssistantPosition::TOP_LEFT_AVOID_IB,
    N_("Top left (avoid InfoBoxes)"),
    N_("Show thermal assistant in top left (avoid InfoBoxes).") },
  { UISettings::ThermalAssistantPosition::TOP_RIGHT_AVOID_IB,
    N_("Top right (avoid InfoBoxes)"),
    N_("Show thermal assistant in top right (avoid InfoBoxes).") },
  { UISettings::ThermalAssistantPosition::CENTER_TOP_AVOID_IB,
    N_("Center top (avoid InfoBoxes)"),
    N_("Show thermal assistant in center top (avoid InfoBoxes).") },
  nullptr
};

std::unique_ptr<Widget>
CreateGaugesConfigPanel()
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();
  const MapSettings &map_settings = CommonInterface::GetMapSettings();

  struct Fields {
    bool enable_gauge;
    bool auto_close_dialog;
    TrafficSettings::GaugeLocation gauge_location;
    UISettings::ThermalAssistantPosition thermal_assistant_position;
    bool show_thermal_profile;
    FinalGlideBarDisplayMode final_glide_bar_display_mode;
    bool final_glide_bar_mc0_enabled;
    bool vario_bar_enabled;
    bool no_position_target_distance_ring;
  };

  auto fields = std::make_shared<Fields>(Fields{
    ui_settings.traffic.enable_gauge,
    ui_settings.traffic.auto_close_dialog,
    ui_settings.traffic.gauge_location,
    ui_settings.thermal_assistant_position,
    map_settings.show_thermal_profile,
    map_settings.final_glide_bar_display_mode,
    map_settings.final_glide_bar_mc0_enabled,
    map_settings.vario_bar_enabled,
    ui_settings.traffic.no_position_target_distance_ring,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

  list->AddSwitch(_("FLARM Radar"),
                  _("This enables the display of the FLARM radar gauge. The track bearing of the target relative to the track bearing of the aircraft is displayed as an arrow head, and a triangle pointing up or down shows the relative altitude of the target relative to you. In all modes, the color of the target indicates the threat level."),
                  fields->enable_gauge);
  list->AddSwitch(_("Auto close FLARM"),
                  _("Setting this to \"On\" will automatically close the FLARM dialog if there is no traffic. \"Off\" will keep the dialog open even without current traffic."),
                  fields->auto_close_dialog, true);
  list->AddEnum(_("FLARM display"),
                _("Choose a location for the FLARM display."),
                flarm_display_location_list, fields->gauge_location, true);
  list->AddEnum(_("Thermal Assistant"),
                _("Enable and select the position of the thermal assistant when overlayed on the main screen."),
                thermal_assistant_position_list,
                fields->thermal_assistant_position);
  list->AddSwitch(_("Thermal Band"),
                  _("This enables the display of the thermal profile (climb band) display on the map."),
                  fields->show_thermal_profile);
  list->AddEnum(_("Final glide bar"),
                _("If set to \"On\" the final glide will always be shown, if set to \"Auto\" it will be shown when approaching the final glide possibility."),
                final_glide_bar_display_mode_list,
                fields->final_glide_bar_display_mode, true);
  list->AddSwitch(_("Final glide bar MC0"),
                  _("If set to \"On\" the final glide bar will show a second arrow indicating the required height "
                      "to reach the final waypoint at MC zero."),
                  fields->final_glide_bar_mc0_enabled, true,
                  [fields] {
                    return fields->final_glide_bar_display_mode !=
                           FinalGlideBarDisplayMode::OFF;
                  });
  list->AddSwitch(_("Vario bar"),
                  _("If set to \"On\" the vario bar will be shown."),
                  fields->vario_bar_enabled, true);
  list->AddSwitch(_("No position target"),
                  _("This parameter enables or disables the No Position Target Distance Ring in Flarm Radar"),
                  fields->no_position_target_distance_ring);

  list->SetSaveCallback([fields](bool &changed) {
    UISettings &ui_settings = CommonInterface::SetUISettings();
    MapSettings &map_settings = CommonInterface::SetMapSettings();

    ConfigPanel::CommitSetting(changed, ui_settings.traffic.enable_gauge,
                               fields->enable_gauge,
                               ProfileKeys::EnableFLARMGauge);
    ConfigPanel::CommitSetting(changed,
                               ui_settings.traffic.auto_close_dialog,
                               fields->auto_close_dialog,
                               ProfileKeys::AutoCloseFlarmDialog);

    if (ConfigPanel::CommitSetting(changed,
                                   ui_settings.thermal_assistant_position,
                                   fields->thermal_assistant_position,
                                   ProfileKeys::TAPosition) |
        ConfigPanel::CommitSetting(changed,
                                   ui_settings.traffic.gauge_location,
                                   fields->gauge_location,
                                   ProfileKeys::FlarmLocation))
      CommonInterface::main_window->ReinitialiseLayout();

    ConfigPanel::CommitSetting(changed, map_settings.show_thermal_profile,
                               fields->show_thermal_profile,
                               ProfileKeys::EnableThermalProfile);
    ConfigPanel::CommitSetting(changed,
                               map_settings.final_glide_bar_display_mode,
                               fields->final_glide_bar_display_mode,
                               ProfileKeys::FinalGlideBarDisplayMode);
    ConfigPanel::CommitSetting(changed,
                               map_settings.final_glide_bar_mc0_enabled,
                               fields->final_glide_bar_mc0_enabled,
                               ProfileKeys::EnableFinalGlideBarMC0);
    ConfigPanel::CommitSetting(changed, map_settings.vario_bar_enabled,
                               fields->vario_bar_enabled,
                               ProfileKeys::EnableVarioBar);
    ConfigPanel::CommitSetting(
        changed, ui_settings.traffic.no_position_target_distance_ring,
        fields->no_position_target_distance_ring,
        ProfileKeys::NoPositionTargetDistanceRing);
    return true;
  });

  return list;
}
