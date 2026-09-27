// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "ConfigMenuData.hpp"
#include "Audio/Features.hpp"
#include "GlideCone/GlideConeCompute.hpp"
#include "Language/Language.hpp"
#include "Tracking/Features.hpp"
#include "net/http/Features.hpp"
#include "util/Macros.hpp"

#include "Panels/AirspaceConfigPanel.hpp"
#include "Panels/AppearanceConfigPanel.hpp"
#include "Panels/GaugesConfigPanel.hpp"
#include "Panels/GlideComputerConfigPanel.hpp"
#include "Panels/GlideConeConfigPanel.hpp"
#include "Panels/HardwareDisplayConfigPanel.hpp"
#include "Panels/HapticsConfigPanel.hpp"
#include "Panels/InfoBoxLayoutConfigPanel.hpp"
#include "Panels/InfoBoxesConfigPanel.hpp"
#include "Panels/InputConfigPanel.hpp"
#include "Panels/LanguageConfigPanel.hpp"
#include "Panels/LoggerConfigPanel.hpp"
#include "Panels/MapDisplayConfigPanel.hpp"
#include "Panels/NetworkConfigPanel.hpp"
#include "Panels/OverlayControlsConfigPanel.hpp"
#include "Panels/PagesConfigPanel.hpp"
#include "Panels/QuickMenuConfigPanel.hpp"
#include "Panels/RaspConfigPanel.hpp"
#include "Panels/RouteConfigPanel.hpp"
#include "Panels/SafetyFactorsConfigPanel.hpp"
#include "Panels/ScoringConfigPanel.hpp"
#include "Panels/SiteConfigPanel.hpp"
#include "Panels/SoundsConfigPanel.hpp"
#include "Panels/SymbolsConfigPanel.hpp"
#include "Panels/TaskDefaultsConfigPanel.hpp"
#include "Panels/TaskRulesConfigPanel.hpp"
#include "Panels/TerrainDisplayConfigPanel.hpp"
#include "Panels/TimeConfigPanel.hpp"
#include "Panels/TopographyDisplayConfigPanel.hpp"
#include "Panels/TrafficSymbolsConfigPanel.hpp"
#include "Panels/UnitsConfigPanel.hpp"
#include "Panels/VarioConfigPanel.hpp"
#include "Panels/WaypointDisplayConfigPanel.hpp"
#include "Panels/WeatherControlsConfigPanel.hpp"
#include "Panels/WeGlideConfigPanel.hpp"

#ifdef HAVE_HTTP
#include "Panels/NOTAMConfigPanel.hpp"
#include "Panels/WeatherConfigPanel.hpp"
#include "Panels/SkySightConfigPanel.hpp"
#include "Panels/XCThermConfigPanel.hpp"
#include "Panels/RainbowConfigPanel.hpp"
#endif

#ifdef HAVE_PCM_PLAYER
#include "Panels/AudioVarioConfigPanel.hpp"
#endif

#ifdef HAVE_VOLUME_CONTROLLER
#include "Panels/AudioConfigPanel.hpp"
#endif

#ifdef HAVE_TRACKING
#include "Panels/TrackingConfigPanel.hpp"
#include "Panels/CloudConfigPanel.hpp"
#endif

#ifdef HAVE_PCMET
#include "Panels/PCMetConfigPanel.hpp"
#endif

#if defined(__linux__) && !defined(__ANDROID__) && !defined(KOBO)
#include "Panels/SystemdConfigPanel.hpp"
#endif

namespace ConfigMenuData {

const TabMenuPage files_pages[] = {
  { N_("Site Files"), CreateSiteConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage map_pages[] = {
  { N_("Orientation"), CreateMapDisplayConfigPanel },
  { N_("Waypoints"), CreateWaypointDisplayConfigPanel },
  { N_("Terrain"), CreateTerrainDisplayConfigPanel },
  { N_("Topology"), CreateTopographyDisplayConfigPanel },
  { N_("Airspace"), CreateAirspaceConfigPanel },
#ifdef HAVE_HTTP
  { NC_("Setting", "NOTAM"), CreateNOTAMConfigPanel },
#endif
  { nullptr, nullptr }
};

const TabMenuPage aircrafts_pages[] = {
  { N_("Aircraft"), CreateSymbolsConfigPanel },
  { N_("Traffic"), CreateTrafficSymbolsConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage gauge_pages[] = {
  { N_("FLARM, Other"), CreateGaugesConfigPanel },
  { N_("Vario"), CreateVarioConfigPanel },
#ifdef HAVE_PCM_PLAYER
  { N_("Audio Vario"), CreateAudioVarioConfigPanel },
#endif
  { nullptr, nullptr }
};

const TabMenuPage infoboxes_pages[] = {
  { N_("Layout"), CreateInfoBoxLayoutConfigPanel },
  { N_("InfoBox Sets"), CreateInfoBoxesConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage pages_pages[] = {
  { N_("Pages"), CreatePagesConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage computer_pages[] = {
  { N_("Safety Factors"), CreateSafetyFactorsConfigPanel },
  { N_("Settings"), CreateGlideComputerConfigPanel },
  { N_("Glide Cone"), CreateGlideConeConfigPanel,
    GlideConeGpuSession::Available },
  { N_("Route"), CreateRouteConfigPanel },
  { N_("Scoring"), CreateScoringConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage task_defaults_pages[] = {
  { N_("Task Rules"), CreateTaskRulesConfigPanel },
  { N_("Turnpoint Types"), CreateTaskDefaultsConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage language_pages[] = {
  { N_("Language"), CreateLanguageConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage hardware_pages[] = {
  { N_("Display"), CreateHardwareDisplayConfigPanel },
  { N_("Haptics"), CreateHapticsConfigPanel },
#ifdef HAVE_VOLUME_CONTROLLER
  { N_("Audio"), CreateAudioConfigPanel },
#endif
  { N_("Network"), CreateNetworkConfigPanel },
#if defined(__linux__) && !defined(__ANDROID__) && !defined(KOBO)
  { N_("Services"), CreateSystemdConfigPanel },
#endif
  { nullptr, nullptr }
};

const TabMenuPage look_accessibility_pages[] = {
  { N_("Input"), CreateInputConfigPanel },
  { N_("Appearance"), CreateAppearanceConfigPanel },
  { N_("Controls"), CreateOverlayControlsConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage units_time_pages[] = {
  { N_("Units"), CreateUnitsConfigPanel },
  // Pages after Units must not have unit-dependent fields that are
  // saved after their units may have changed.
  { NC_("Setting", "Time"), CreateTimeConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage setup_pages[] = {
  { N_("Quick Menu"), CreateQuickMenuConfigPanel },
  { N_("Sounds"), CreateSoundsConfigPanel },
  { N_("Logger"), CreateLoggerConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage weather_pages[] = {
#ifdef HAVE_HTTP
  { N_("Thermal Information Map"), CreateWeatherConfigPanel },
#endif
  { "RASP", CreateRaspConfigPanel },
#ifdef HAVE_HTTP
  { "SkySight", CreateSkySightConfigPanel },
#endif
#ifdef HAVE_PCMET
  { "Flugwetter (pc_met)", CreatePCMetConfigPanel },
#endif
#ifdef HAVE_HTTP
  { "Rainbow", CreateRainbowConfigPanel },
  { "XC Therm", CreateXCThermConfigPanel },
#endif
  { N_("Controls"), CreateWeatherControlsConfigPanel },
  { nullptr, nullptr }
};

const TabMenuPage accounts_pages[] = {
#ifdef HAVE_TRACKING
  { N_("Tracking"), CreateTrackingConfigPanel },
  { "XCSoar Cloud", CreateCloudConfigPanel },
#endif
  { "WeGlide", CreateWeGlideConfigPanel },
  { nullptr, nullptr }
};

const TabMenuGroup list_groups[] = {
  { N_("Site Files"), files_pages },
  { N_("Map"), map_pages },
  { N_("Aircrafts"), aircrafts_pages },
  { N_("Gauges"), gauge_pages },
  { N_("InfoBoxes"), infoboxes_pages },
  { N_("Pages"), pages_pages },
  { N_("Glide Computer"), computer_pages },
  { N_("Task Defaults"), task_defaults_pages },
  { N_("Language"), language_pages },
  { N_("Hardware"), hardware_pages },
  { N_("Look & Accessibility"), look_accessibility_pages },
  { N_("Units & Time"), units_time_pages },
  { NC_("Menu", "Setup"), setup_pages },
  { N_("Weather"), weather_pages },
  { N_("Accounts & Services"), accounts_pages },
};

const unsigned list_group_count = ARRAY_SIZE(list_groups);

/* TabMenuDisplay::MAX_MAIN_MENU_ITEMS must stay large enough. */
static_assert(ARRAY_SIZE(list_groups) <= 16);

} // namespace ConfigMenuData
