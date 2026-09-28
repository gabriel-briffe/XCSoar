// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TrafficSymbolsConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MapSettings.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "util/Macros.hpp"

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

/** How other aircraft are drawn on the map. */
class TrafficSymbolsConfigPanel final : public ConfigListPanel {
  bool show_flarm_on_map;
  TrafficSymbol traffic_symbol;
  AircraftTypeSymbolStyle traffic_symbol_style;
  bool fade_traffic;
  DisplayOnlineTrafficMapMode online_traffic_map_mode;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
TrafficSymbolsConfigPanel::LoadSettings() noexcept
{
  const MapSettings &settings_map = CommonInterface::GetMapSettings();

  show_flarm_on_map = settings_map.show_flarm_on_map;
  traffic_symbol = settings_map.traffic_symbol;
  traffic_symbol_style = settings_map.traffic_symbol_style;
  fade_traffic = settings_map.fade_traffic;
  online_traffic_map_mode = settings_map.online_traffic_map_mode;
}

void
TrafficSymbolsConfigPanel::Fill() noexcept
{
  AddGroup();

  AddToggleItem(_("FLARM Traffic"),
                _("This enables the display of FLARM traffic on the map window."),
                show_flarm_on_map);
  AddEnumItem(_("Traffic symbol"),
              _("Determines how FLARM, ADS-B and online traffic is drawn on the map and the traffic radar."),
              traffic_symbol_list, traffic_symbol);

  if (traffic_symbol == TrafficSymbol::AIRCRAFT_TYPE)
    AddEnumItem(_("Traffic symbol style"),
                _("How aircraft-type traffic symbols are coloured (halo and glyph)."),
                traffic_symbol_style_list, traffic_symbol_style);

  AddToggleItem(_("Fade traffic"),
                _("Keep showing traffic for a while after it has disappeared."),
                fade_traffic);
  AddEnumItem(C_("Setting", "Online traffic on map"),
              _("Show traffic from SkyLines and XCSoar Cloud on the map."),
              online_traffic_map_mode_list, online_traffic_map_mode);
}

bool
TrafficSymbolsConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  MapSettings &settings_map = CommonInterface::SetMapSettings();

  changed |= Profile::Update(ProfileKeys::EnableFLARMMap,
                             settings_map.show_flarm_on_map,
                             show_flarm_on_map);
  changed |= Profile::Update(ProfileKeys::TrafficSymbol,
                             settings_map.traffic_symbol, traffic_symbol);
  changed |= Profile::Update(ProfileKeys::TrafficSymbolStyle,
                             settings_map.traffic_symbol_style,
                             traffic_symbol_style);
  changed |= Profile::Update(ProfileKeys::FadeTraffic,
                             settings_map.fade_traffic, fade_traffic);
  changed |= Profile::Update(ProfileKeys::OnlineTrafficMapMode,
                             settings_map.online_traffic_map_mode,
                             online_traffic_map_mode);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateTrafficSymbolsConfigPanel()
{
  return std::make_unique<TrafficSymbolsConfigPanel>();
}
