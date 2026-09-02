// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InputEvents.hpp"
#include "Language/Language.hpp"
#include "Message.hpp"
#include "MapSettings.hpp"
#include "Interface.hpp"
#include "ActionInterface.hpp"
#include "Components.hpp"
#include "BackendComponents.hpp"
#include "DataComponents.hpp"
#include "Airspace/ProtectedAirspaceWarningManager.hpp"
#include "Airspace/AirspaceVisibility.hpp"
#include "Engine/Airspace/AirspaceAircraftPerformance.hpp"
#include "Engine/Airspace/SoonestAirspace.hpp"
#include "Dialogs/Airspace/Airspace.hpp"
#include "Dialogs/Airspace/AirspaceWarningDialog.hpp"
#include "NMEA/Aircraft.hpp"
#include "PageSetting.hpp"
#include "util/StringCompare.hxx"

/*
 * Temporary map-display airspace on/off (not a global profile setting).
 * Page-only membership stores the choice in per-page overrides.
 */
void
InputEvents::eventAirSpace(const char *misc)
{
  const bool currently_on =
    CommonInterface::GetMapSettings().airspace.enable;

  if (StringIsEqual(misc, "show")) {
    Message::AddMessage(currently_on
                        ? _("Show airspace on")
                        : _("Show airspace off"));
    return;
  }

  if (StringIsEqual(misc, "list")) {
    ShowAirspaceListDialog(*data_components->airspaces,
                           backend_components->GetAirspaceWarnings());
    return;
  }

  int value = currently_on ? 1 : 0;
  if (StringIsEqual(misc, "toggle"))
    value = value ? 0 : 1;
  else if (StringIsEqual(misc, "off"))
    value = 0;
  else if (StringIsEqual(misc, "on"))
    value = 1;
  else
    return;

  Message::AddMessage(value
                      ? _("Airspace shown")
                      : _("Airspace hidden"));
  PageSettingApplyCommand(PageSettingId::AIRSPACE_ENABLE, value);
}

void
InputEvents::eventAirspaceLabels(const char *misc)
{
  using LabelSelection = AirspaceRendererSettings::LabelSelection;

  const bool currently_on =
    CommonInterface::GetMapSettings().airspace.label_selection ==
    LabelSelection::ALL;

  if (StringIsEqual(misc, "show")) {
    Message::AddMessage(currently_on
                        ? _("Airspace labels on")
                        : _("Airspace labels off"));
    return;
  }

  int value = currently_on ? int(LabelSelection::ALL)
                           : int(LabelSelection::NONE);
  if (StringIsEqual(misc, "toggle"))
    value = currently_on ? int(LabelSelection::NONE)
                         : int(LabelSelection::ALL);
  else if (StringIsEqual(misc, "off") || StringIsEqual(misc, "none"))
    value = int(LabelSelection::NONE);
  else if (StringIsEqual(misc, "on") || StringIsEqual(misc, "all"))
    value = int(LabelSelection::ALL);
  else
    return;

  Message::AddMessage(value == int(LabelSelection::NONE)
                      ? _("Airspace labels hidden")
                      : _("Airspace labels shown"));
  PageSettingApplyCommand(PageSettingId::AIRSPACE_LABEL_VISIBILITY, value);
}

// ClearAirspaceWarnings
// Clears airspace warnings for the selected airspace
void
InputEvents::eventClearAirspaceWarnings([[maybe_unused]] const char *misc)
{
  if (auto *airspace_warnings = backend_components->GetAirspaceWarnings())
    airspace_warnings->AcknowledgeAll();
}

// AirspaceWarnings
// Shows the airspace warnings dialog
void
InputEvents::eventAirspaceWarnings([[maybe_unused]] const char *misc)
{
  auto *airspace_warnings = backend_components->GetAirspaceWarnings();
  if (airspace_warnings != nullptr)
    dlgAirspaceWarningsShowModal(*airspace_warnings);
}

// NearestAirspaceDetails
// Displays details of the nearest airspace to the aircraft in a
// status message.  This does nothing if there is no airspace within
// 100km of the aircraft.
// If the aircraft is within airspace, this displays the distance and bearing
// to the nearest exit to the airspace.

void 
InputEvents::eventNearestAirspaceDetails([[maybe_unused]] const char *misc)
{
  const MoreData &basic = CommonInterface::Basic();
  const DerivedInfo &calculated = CommonInterface::Calculated();
  const ComputerSettings &settings_computer =
    CommonInterface::GetComputerSettings();

  auto *airspace_warnings = backend_components->GetAirspaceWarnings();
  if (airspace_warnings != nullptr && !airspace_warnings->IsEmpty()) {
    // Prevent the dialog from closing itself without active warning
    // This is relevant if there are only acknowledged airspaces in the list
    // AutoClose will be reset when the dialog is closed again by hand
    dlgAirspaceWarningsShowModal(*airspace_warnings);
    return;
  }

  const AircraftState aircraft_state =
    ToAircraftState(basic, calculated);
  auto visible = AirspaceVisiblePredicate(settings_computer.airspace,
                                          CommonInterface::GetMapSettings().airspace,
                                          aircraft_state);
  GlidePolar polar = settings_computer.polar.glide_polar_task;
  polar.SetMC(std::max(polar.GetMC(), 1.));
  const AirspaceAircraftPerformance perf(polar);

  const auto as = FindSoonestAirspace(*data_components->airspaces, aircraft_state, perf,
                                      std::move(visible),
                                      std::chrono::minutes{30});
  if (!as) {
    return;
  } 

  dlgAirspaceDetails(std::move(as), airspace_warnings);
}
