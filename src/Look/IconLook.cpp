// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "IconLook.hpp"
#include "Resources.hpp"

void
IconLook::Initialise()
{
  hBmpTabTask.LoadResource(IDB_TASK_ALL);
  hBmpTabWrench.LoadResource(IDB_WRENCH_ALL);
  hBmpTabSettings.LoadResource(IDB_SETTINGS_ALL);
  hBmpTabCalculator.LoadResource(IDB_CALCULATOR_ALL);

  hBmpTabFlight.LoadResource(IDB_GLOBE_ALL);
  hBmpTabSystem.LoadResource(IDB_DEVICE_ALL);
  hBmpTabRules.LoadResource(IDB_RULES_ALL);
  hBmpTabTimes.LoadResource(IDB_CLOCK_ALL);

  hBmpConfigPlanes.LoadResource(IDB_POLAR_ALL);
  hBmpConfigProfiles.LoadResource(IDB_USER_ALL);
  hBmpConfigFlightSetup.LoadResource(IDB_TAKEOFF_ALL);
  hBmpConfigWind.LoadResource(IDB_WIND_ALL);
  hBmpConfigDataManagement.LoadResource(IDB_DATABASE_ALL);
  hBmpConfigWaypointEditor.LoadResource(IDB_WAYPOINT_EDIT_ALL);
  hBmpConfigReplay.LoadResource(IDB_REPLAY_ALL);
  hBmpConfigLoggerStart.LoadResource(IDB_LOGGER_START_ALL);
  hBmpConfigLua.LoadResource(IDB_LUA_ALL);
  hBmpConfigUpload.LoadResource(IDB_UPLOAD_ALL);
  hBmpConfigHidden.LoadResource(IDB_HIDDEN_ALL);
  hBmpConfigFlightDisplay.LoadResource(IDB_FLIGHT_DISPLAY_ALL);
  hBmpConfigPages.LoadResource(IDB_PAGES_ALL);
  hBmpConfigDisplay.LoadResource(IDB_DISPLAY_ALL);
  hBmpConfigSounds.LoadResource(IDB_SOUNDS_ALL);
  hBmpConfigLanguage.LoadResource(IDB_LANGUAGE_ALL);
  hBmpConfigQuit.LoadResource(IDB_QUIT_ALL);
  hBmpConfigMap.LoadResource(IDB_MAP_ALL);
  hBmpConfigGauges.LoadResource(IDB_GAUGES_ALL);
  hBmpConfigGlideComputer.LoadResource(IDB_GLIDE_COMPUTER_ALL);
  hBmpConfigInfoBoxes.LoadResource(IDB_INFOBOXES_ALL);
  hBmpConfigOrientation.LoadResource(IDB_ORIENTATION_ALL);
  hBmpConfigWaypoints.LoadResource(IDB_WAYPOINTS_ALL);
  hBmpConfigAirspace.LoadResource(IDB_AIRSPACE_ICON_ALL);
  hBmpConfigAircraft.LoadResource(IDB_AIRCRAFT_ALL);
  hBmpConfigAircrafts.LoadResource(IDB_AIRCRAFTS_ALL);
  hBmpConfigTraffic.LoadResource(IDB_TRAFFIC_ALL);
  hBmpConfigTerrain.LoadResource(IDB_TERRAIN_ICON_ALL);
  hBmpConfigTopology.LoadResource(IDB_TOPOLOGY_ALL);
  hBmpConfigSafetyFactors.LoadResource(IDB_SAFETY_FACTORS_ALL);
  hBmpConfigSettingsWrench.LoadResource(IDB_SETTINGS_WRENCH_ALL);
}
