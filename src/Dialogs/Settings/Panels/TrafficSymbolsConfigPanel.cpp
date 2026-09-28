// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TrafficSymbolsConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MapSettings.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/Macros.hpp"

#include <memory>

static constexpr StaticEnumChoice traffic_symbol_list[] = {
  { TrafficSymbol::ARROW, N_("Arrow"),
    N_("All traffic is drawn as an arrow head pointing in the direction of travel.") },
  { TrafficSymbol::AIRCRAFT_TYPE, N_("Aircraft type"),
    N_("Traffic is drawn as a glider, powered aircraft, helicopter, paraglider "
       "or other symbol according to the aircraft type reported by FLARM, ADS-B "
       "or OGN. Targets of unknown type get a generic symbol.") },
  nullptr
};

static_assert(ARRAY_SIZE(traffic_symbol_list) ==
              unsigned(TrafficSymbol::COUNT) + 1);

static constexpr StaticEnumChoice traffic_symbol_style_list[] = {
  { AircraftTypeSymbolStyle::COLOURED_HALO, N_("Colored halo"),
    N_("The silhouette is filled with the traffic colour; the glyph is black.") },
  { AircraftTypeSymbolStyle::COLOURED_HALO_OUTLINED, N_("Colored halo with border"),
    N_("Like Colored halo, plus a thin border around the silhouette.") },
  { AircraftTypeSymbolStyle::WHITE_HALO, N_("White halo, colored glyph"),
    N_("White silhouette with the traffic colour in the glyph. Calm on the map; "
       "the halo vanishes on a white radar background.") },
  { AircraftTypeSymbolStyle::BLACK_OUTLINE, N_("Black outline"),
    N_("No halo; the glyph is filled with the traffic colour and outlined in black, "
       "like the classic arrow head.") },
  nullptr
};

static_assert(ARRAY_SIZE(traffic_symbol_style_list) ==
              unsigned(AircraftTypeSymbolStyle::COUNT) + 1);

static constexpr StaticEnumChoice online_traffic_map_mode_list[] = {
  { DisplayOnlineTrafficMapMode::OFF, N_("Off"), N_("No online traffic is drawn.") },
  { DisplayOnlineTrafficMapMode::SYMBOL, N_("Symbol"), N_("Draws the traffic symbol only.") },
  { DisplayOnlineTrafficMapMode::SYMBOL_NAME, N_("Symbol and Name"), N_("Draws the traffic symbol with name.") },
  nullptr
};

std::unique_ptr<Widget>
CreateTrafficSymbolsConfigPanel()
{
  const MapSettings &settings_map = CommonInterface::GetMapSettings();

  struct Fields {
    bool show_flarm_on_map;
    TrafficSymbol traffic_symbol;
    AircraftTypeSymbolStyle traffic_symbol_style;
    bool fade_traffic;
    DisplayOnlineTrafficMapMode online_traffic_map_mode;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings_map.show_flarm_on_map,
    settings_map.traffic_symbol,
    settings_map.traffic_symbol_style,
    settings_map.fade_traffic,
    settings_map.online_traffic_map_mode,
  });

  const auto style_shown = [fields] {
    return fields->traffic_symbol == TrafficSymbol::AIRCRAFT_TYPE;
  };

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddSwitch(_("FLARM Traffic"),
                  _("This enables the display of FLARM traffic on the map window."),
                  fields->show_flarm_on_map);
  list->AddEnum(_("Traffic symbol"),
                _("Determines how FLARM, ADS-B and online traffic is drawn on the map and the traffic radar."),
                traffic_symbol_list, fields->traffic_symbol);
  list->AddEnum(_("Traffic symbol style"),
                _("How aircraft-type traffic symbols are coloured (halo and glyph)."),
                traffic_symbol_style_list,
                fields->traffic_symbol_style, false, style_shown);
  list->AddSwitch(_("Fade traffic"),
                  _("Keep showing traffic for a while after it has disappeared."),
                  fields->fade_traffic);
  list->AddEnum(C_("Setting", "Online traffic on map"),
                _("Show traffic from SkyLines and XCSoar Cloud on the map."),
                online_traffic_map_mode_list,
                fields->online_traffic_map_mode);

  list->SetSaveCallback([fields](bool &changed) {
    MapSettings &settings_map = CommonInterface::SetMapSettings();

    ConfigPanel::CommitSetting(changed, settings_map.show_flarm_on_map,
                               fields->show_flarm_on_map,
                               ProfileKeys::EnableFLARMMap);
    ConfigPanel::CommitSetting(changed, settings_map.traffic_symbol,
                               fields->traffic_symbol,
                               ProfileKeys::TrafficSymbol);
    ConfigPanel::CommitSetting(changed, settings_map.traffic_symbol_style,
                               fields->traffic_symbol_style,
                               ProfileKeys::TrafficSymbolStyle);
    ConfigPanel::CommitSetting(changed, settings_map.fade_traffic,
                               fields->fade_traffic,
                               ProfileKeys::FadeTraffic);
    ConfigPanel::CommitSetting(changed, settings_map.online_traffic_map_mode,
                               fields->online_traffic_map_mode,
                               ProfileKeys::OnlineTrafficMapMode);
    return true;
  });

  return list;
}
