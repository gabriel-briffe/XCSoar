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
}
