// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "ui/canvas/Icon.hpp"

/**
 * This class manages the icons of various XCSoar dialogs.
 */
struct IconLook {
  // task dialog
  MaskedIcon hBmpTabTask;
  MaskedIcon hBmpTabWrench;
  MaskedIcon hBmpTabSettings;
  MaskedIcon hBmpTabCalculator;

  // status dialog
  MaskedIcon hBmpTabFlight;
  MaskedIcon hBmpTabSystem;
  MaskedIcon hBmpTabRules;
  MaskedIcon hBmpTabTimes;

  // config menu tiles
  MaskedIcon hBmpConfigPlanes;
  MaskedIcon hBmpConfigProfiles;
  MaskedIcon hBmpConfigFlightSetup;
  MaskedIcon hBmpConfigWind;
  MaskedIcon hBmpConfigDataManagement;
  MaskedIcon hBmpConfigWaypointEditor;
  MaskedIcon hBmpConfigReplay;
  MaskedIcon hBmpConfigLoggerStart;
  MaskedIcon hBmpConfigLua;
  MaskedIcon hBmpConfigUpload;
  MaskedIcon hBmpConfigHidden;
  MaskedIcon hBmpConfigFlightDisplay;
  MaskedIcon hBmpConfigPages;
  MaskedIcon hBmpConfigDisplay;
  MaskedIcon hBmpConfigSounds;
  MaskedIcon hBmpConfigLanguage;
  MaskedIcon hBmpConfigQuit;
  MaskedIcon hBmpConfigMap;
  MaskedIcon hBmpConfigGauges;
  MaskedIcon hBmpConfigGlideComputer;
  MaskedIcon hBmpConfigInfoBoxes;
  MaskedIcon hBmpConfigOrientation;
  MaskedIcon hBmpConfigWaypoints;
  MaskedIcon hBmpConfigAirspace;
  MaskedIcon hBmpConfigAircraft;
  MaskedIcon hBmpConfigAircrafts;
  MaskedIcon hBmpConfigTraffic;
  MaskedIcon hBmpConfigTerrain;
  MaskedIcon hBmpConfigTopology;
  MaskedIcon hBmpConfigSafetyFactors;
  MaskedIcon hBmpConfigSettingsWrench;

  void Initialise();
};
