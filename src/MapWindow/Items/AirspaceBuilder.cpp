// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Builder.hpp"
#include "MapItem.hpp"
#include "List.hpp"
#include "Airspace/AirspaceMapVisible.hpp"
#include "Airspace/AirspaceWarningCopy.hpp"
#include "Engine/Airspace/AbstractAirspace.hpp"
#include "Engine/Airspace/Airspaces.hpp"
#include "Airspace/ProtectedAirspaceWarningManager.hpp"
#include "NMEA/Aircraft.hpp"

void
MapItemListBuilder::AddVisibleAirspace(
    const Airspaces &airspaces,
    const ProtectedAirspaceWarningManager *warning_manager,
    const AirspaceComputerSettings &computer_settings,
    const AirspaceRendererSettings &renderer_settings,
    const MoreData &basic, const DerivedInfo &calculated)
{
  AirspaceWarningCopy warnings;
  if (warning_manager != nullptr)
    warnings.Visit(*warning_manager);

  const AircraftState aircraft = ToAircraftState(basic, calculated);
  const AirspaceMapVisible visible(computer_settings, renderer_settings,
                                   aircraft, warnings);

  for (const auto &i : airspaces.QueryWithinRange(location, 100)) {
    if (list.full())
      break;

    auto airspace = i.GetAirspacePtr();
    if (visible(*airspace) && airspace->Inside(location))
      list.append(new AirspaceMapItem(std::move(airspace)));
  }
}
