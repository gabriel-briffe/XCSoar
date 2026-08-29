// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Airspace/AirspaceVisibility.hpp"
#include "Airspace/AirspaceWarningCopy.hpp"
#include "Engine/Airspace/AbstractAirspace.hpp"

struct AirspaceComputerSettings;
struct AirspaceRendererSettings;
struct AircraftState;

/**
 * Airspace visibility predicate shared by map rendering and the map-item
 * list ("what's here").
 */
class AirspaceMapVisible {
  const AirspaceVisibility visible_predicate;
  const AirspaceWarningCopy &warnings;

public:
  AirspaceMapVisible(const AirspaceComputerSettings &computer_settings,
                     const AirspaceRendererSettings &renderer_settings,
                     const AircraftState &state,
                     const AirspaceWarningCopy &warnings) noexcept
    :visible_predicate(computer_settings, renderer_settings, state),
     warnings(warnings) {}

  [[gnu::pure]]
  bool operator()(const AbstractAirspace &airspace) const noexcept {
    return visible_predicate(airspace) ||
      warnings.IsInside(airspace) ||
      warnings.HasWarning(airspace);
  }
};
