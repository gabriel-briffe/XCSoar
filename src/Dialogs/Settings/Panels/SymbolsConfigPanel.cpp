// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SymbolsConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MapSettings.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>
template<typename T>
static void
AddLinkedEnum(GroupedListWidget &list, const char *caption,
              const char *help, const StaticEnumChoice *choices,
              T &value, bool expert) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.expert = expert;
  options.value_callback =
    [choices, &value](GroupedListWidget::ValueState &state) {
      state.text.clear();
      for (auto i = choices; i->display_string != nullptr; ++i)
        if (i->id == static_cast<unsigned>(value))
          state.text = gettext(i->display_string);
    };
  list.AddValue(caption, [&list, caption, help, choices, &value] {
    DataFieldEnum df;
    df.EnableItemHelp(choices->help != nullptr);
    df.AddChoices(choices);
    df.SetValue(static_cast<unsigned>(value));
    if (!ComboPicker(caption, df, help) ||
        df.GetValue() == static_cast<unsigned>(value))
      return;
    value = static_cast<T>(df.GetValue());
    if (list.UpdateValues())
      list.UpdateLayout();
  }, std::move(options));
}

static constexpr StaticEnumChoice ground_track_mode_list[] = {
  { DisplayGroundTrack::OFF, N_("Off"), N_("Disable display of ground track line.") },
  { DisplayGroundTrack::ON, N_("On"), N_("Always display ground track line.") },
  { DisplayGroundTrack::AUTO, NC_("Setting", "Auto"), N_("Display ground track line if there is a significant difference to plane heading.") },
  nullptr
};

static constexpr StaticEnumChoice trail_length_list[] = {
  { TrailSettings::Length::OFF, N_("Off") },
  { TrailSettings::Length::LONG, N_("Long") },
  { TrailSettings::Length::SHORT, N_("Short") },
  { TrailSettings::Length::FULL, N_("Full") },
  nullptr
};

static constexpr StaticEnumChoice trail_type_list[] = {
  { TrailSettings::Type::VARIO_1, N_("Vario #1"), N_("Within lift areas "
    "lines get displayed green and thicker, while sinking lines are shown brown and thin. "
    "Zero lift is presented as a grey line.") },
  { TrailSettings::Type::VARIO_1_DOTS, N_("Vario #1 (with dots)"), N_("The same "
    "colour scheme as the previous, but with dotted lines while sinking.") },
  { TrailSettings::Type::VARIO_2, N_("Vario #2"), N_("The climb colour "
    "for this scheme is orange to red, sinking is displayed as light blue to dark blue. "
    "Zero lift is presented as a yellow line.") },
  { TrailSettings::Type::VARIO_2_DOTS, N_("Vario #2 (with dots)"), N_("The same "
    "colour scheme as the previous, but with dotted lines while sinking.") },
  { TrailSettings::Type::VARIO_DOTS_AND_LINES,
    N_("Vario-scaled dots and lines"),
    N_("Vario-scaled dots with lines. "
       "Orange to red = climb. Light blue to dark blue = sink. "
       "Zero lift is presented as a yellow line.") },
  { TrailSettings::Type::VARIO_EINK, N_("Vario E-ink"), N_("E-ink friendly color scheme, lighter and thicker dots means lift while darker and thinner means sink.") },
  { TrailSettings::Type::ALTITUDE, N_("Altitude"), N_("The colour scheme corresponds to the height.") },
  nullptr
};

static constexpr StaticEnumChoice  aircraft_symbol_list[] = {
  { AircraftSymbol::SIMPLE, N_("Simple"),
    N_("Simplified line graphics, black with white contours.") },
  { AircraftSymbol::SIMPLE_LARGE, N_("Simple (large)"),
    N_("Enlarged simple graphics.") },
  { AircraftSymbol::DETAILED, N_("Detailed"),
    N_("Detailed rendered aircraft graphics.") },
  { AircraftSymbol::HANGGLIDER, N_("HangGlider"),
    N_("Simplified hang glider as line graphics, white with black contours.") },
  { AircraftSymbol::PARAGLIDER, N_("Paraglider"),
    N_("Simplified para glider as line graphics, white with black contours.") },
  nullptr
};

static constexpr StaticEnumChoice wind_arrow_list[] = {
  { WindArrowStyle::NO_ARROW, N_("Off"), N_("No wind arrow is drawn.") },
  { WindArrowStyle::ARROW_HEAD, N_("Arrow head"), N_("Draws an arrow head only.") },
  { WindArrowStyle::FULL_ARROW, N_("Full arrow"), N_("Draws an arrow head with a dashed arrow line.") },
  nullptr
};


std::unique_ptr<Widget>
CreateSymbolsConfigPanel()
{
  const MapSettings &settings_map = CommonInterface::GetMapSettings();

  struct Fields {
    DisplayGroundTrack ground_track;
    TrailSettings::Length trail_length;
    bool trail_vbo;
    bool trail_drift;
    TrailSettings::Type trail_type;
    bool trail_scaled;
    bool thermal_marker;
    bool detour_cost;
    AircraftSymbol aircraft_symbol;
    WindArrowStyle wind_arrow;
    bool distance_rings;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings_map.display_ground_track,
    settings_map.trail.length,
    settings_map.trail.vbo,
    settings_map.trail.wind_drift_enabled,
    settings_map.trail.type,
    settings_map.trail.scaling_enabled,
    settings_map.show_thermal_marker,
    settings_map.detour_cost_markers_enabled,
    settings_map.aircraft_symbol,
    settings_map.wind_arrow_style,
    settings_map.distance_rings_enabled,
  });

  const auto trail_shown = [fields] {
    return fields->trail_length != TrailSettings::Length::OFF;
  };

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddEnum(_("Ground track"),
                _("Display the ground track as a grey line on the map."),
                ground_track_mode_list, fields->ground_track);
  AddLinkedEnum(*list, _("Trail length"),
                _("Determines whether and how long a snail trail is drawn behind the glider."),
                trail_length_list, fields->trail_length, true);
  list->AddSwitch(_("Trail VBO"),
                  _("OpenGL only: for Full trail length, keep the path in a GPU "
                    "vertex buffer (no screen-space thinning). Off uses the "
                    "normal CPU trail path."),
                  fields->trail_vbo, true, trail_shown);
  list->AddSwitch(_("Trail drift"),
                  _("Determines whether the snail trail is drifted with the wind "
                    "when displayed in circling mode at near map scales. Switched "
                    "Off, the snail trail stays uncompensated for wind drift."),
                  fields->trail_drift, true, trail_shown);
  list->AddEnum(_("Trail type"),
                _("Sets the type of the snail trail display."),
                trail_type_list, fields->trail_type, true, trail_shown);
  list->AddSwitch(_("Trail scaled"),
                  _("If set to ON the snail trail width is scaled according to the vario signal."),
                  fields->trail_scaled, true, trail_shown);
  list->AddSwitch(_("Thermal marker"),
                  _("Show thermal locator markers (spirals) for recent thermals on the map."),
                  fields->thermal_marker);
  list->AddSwitch(_("Detour cost markers"),
                  _("If the aircraft heading deviates from the current waypoint, markers are displayed "
                    "at points ahead of the aircraft. The value of each marker is the extra distance "
                    "required to reach that point as a percentage of straight-line distance to the waypoint."),
                  fields->detour_cost, true);
  list->AddEnum(_("Aircraft symbol"), nullptr, aircraft_symbol_list,
                fields->aircraft_symbol, true);
  list->AddEnum(_("Wind arrow"),
                _("Determines the way the wind arrow is drawn on the map."),
                wind_arrow_list, fields->wind_arrow, true);
  list->AddSwitch(C_("Setting", "Distance rings"),
                  _("Display distance rings around the aircraft on the map."),
                  fields->distance_rings);

  list->SetSaveCallback([fields](bool &changed) {
    MapSettings &settings_map = CommonInterface::SetMapSettings();

    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.display_ground_track, fields->ground_track,
      ProfileKeys::DisplayTrackBearing);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.trail.length, fields->trail_length,
      ProfileKeys::SnailTrail);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.trail.vbo, fields->trail_vbo,
      ProfileKeys::SnailTrailVBO);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.trail.wind_drift_enabled, fields->trail_drift,
      ProfileKeys::TrailDrift);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.trail.type, fields->trail_type,
      ProfileKeys::SnailType);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.trail.scaling_enabled, fields->trail_scaled,
      ProfileKeys::SnailWidthScale);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.show_thermal_marker, fields->thermal_marker,
      ProfileKeys::EnableThermalMarker);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.detour_cost_markers_enabled, fields->detour_cost,
      ProfileKeys::DetourCostMarker);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.aircraft_symbol, fields->aircraft_symbol,
      ProfileKeys::AircraftSymbol);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.wind_arrow_style, fields->wind_arrow,
      ProfileKeys::WindArrowStyle);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.distance_rings_enabled, fields->distance_rings,
      ProfileKeys::DistanceRingsEnabled);
    return true;
  });

  return list;
}
