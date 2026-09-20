// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TrafficSymbolsConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Listener.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "MapSettings.hpp"
#include "util/Macros.hpp"

enum ControlIndex {
  ENABLE_FLARM_MAP,
  TRAFFIC_SYMBOL,
  TRAFFIC_SYMBOL_STYLE,
  FADE_TRAFFIC,
  SKYLINES_TRAFFIC_MAP_MODE,
};

class TrafficSymbolsConfigPanel final
  : public RowFormWidget, DataFieldListener {
public:
  TrafficSymbolsConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void ShowTrafficSymbolStyle(bool show);

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;

private:
  void OnModified(DataField &df) noexcept override;
};

void
TrafficSymbolsConfigPanel::ShowTrafficSymbolStyle(bool show)
{
  SetRowVisible(TRAFFIC_SYMBOL_STYLE, show);
}

void
TrafficSymbolsConfigPanel::OnModified(DataField &df) noexcept
{
  if (IsDataField(TRAFFIC_SYMBOL, df)) {
    const DataFieldEnum &dfe = (const DataFieldEnum &)df;
    ShowTrafficSymbolStyle(TrafficSymbol(dfe.GetValue()) ==
                           TrafficSymbol::AIRCRAFT_TYPE);
  }
}

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

void
TrafficSymbolsConfigPanel::Prepare([[maybe_unused]] ContainerWindow &parent,
                                   [[maybe_unused]] const PixelRect &rc) noexcept
{
  const MapSettings &settings_map = CommonInterface::GetMapSettings();

  AddBoolean(_("FLARM Traffic"),
             _("This enables the display of FLARM traffic on the map window."),
             settings_map.show_flarm_on_map);

  AddEnum(_("Traffic symbol"),
          _("Determines how FLARM, ADS-B and online traffic is drawn on the map and the traffic radar."),
          traffic_symbol_list, (unsigned)settings_map.traffic_symbol, this);

  AddEnum(_("Traffic symbol style"),
          _("How aircraft-type traffic symbols are coloured (halo and glyph)."),
          traffic_symbol_style_list,
          (unsigned)settings_map.traffic_symbol_style);

  AddBoolean(_("Fade traffic"),
             _("Keep showing traffic for a while after it has disappeared."),
             settings_map.fade_traffic);

  AddEnum(C_("Setting", "Online traffic on map"),
          _("Show traffic from SkyLines and XCSoar Cloud on the map."),
          online_traffic_map_mode_list,
          (unsigned)settings_map.online_traffic_map_mode);

  ShowTrafficSymbolStyle(settings_map.traffic_symbol ==
                         TrafficSymbol::AIRCRAFT_TYPE);
}

bool
TrafficSymbolsConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;

  MapSettings &settings_map = CommonInterface::SetMapSettings();

  changed |= SaveValue(ENABLE_FLARM_MAP, ProfileKeys::EnableFLARMMap,
                       settings_map.show_flarm_on_map);

  changed |= SaveValueEnum(TRAFFIC_SYMBOL, ProfileKeys::TrafficSymbol,
                           settings_map.traffic_symbol);

  changed |= SaveValueEnum(TRAFFIC_SYMBOL_STYLE,
                           ProfileKeys::TrafficSymbolStyle,
                           settings_map.traffic_symbol_style);

  changed |= SaveValue(FADE_TRAFFIC, ProfileKeys::FadeTraffic,
                       settings_map.fade_traffic);

  changed |= SaveValueEnum(SKYLINES_TRAFFIC_MAP_MODE,
                           ProfileKeys::OnlineTrafficMapMode,
                           settings_map.online_traffic_map_mode);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateTrafficSymbolsConfigPanel()
{
  return std::make_unique<TrafficSymbolsConfigPanel>();
}
