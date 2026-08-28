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
#include "PageActions.hpp"
#include "NMEA/Aircraft.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "util/StringCompare.hxx"

/*
 * Toggles airspace rendering on the map.  On configured pages the
 * show/hide state is remembered per page (Config → Look → Pages does
 * not expose it; use the Display menu or Quick Menu).  On special
 * pages the toggle is temporary until Restore().
 */
void
InputEvents::eventAirSpace(const char *misc)
{
  AirspaceRendererSettings &settings =
    CommonInterface::SetMapSettings().airspace;

  if (StringIsEqual(misc, "toggle")) {
    settings.enable = !settings.enable;
    Message::AddMessage(settings.enable
                        ? _("Airspace shown")
                        : _("Airspace hidden"));
  } else if (StringIsEqual(misc, "off")) {
    settings.enable = false;
    Message::AddMessage(_("Airspace hidden"));
  } else if (StringIsEqual(misc, "on")) {
    settings.enable = true;
    Message::AddMessage(_("Airspace shown"));
  }
  else if (StringIsEqual(misc, "show")) {
    if (!settings.enable)
      Message::AddMessage(_("Show airspace off"));
    if (settings.enable)
      Message::AddMessage(_("Show airspace on"));
    return;
  } else if (StringIsEqual(misc, "list")) {
    ShowAirspaceListDialog(*data_components->airspaces,
                           backend_components->GetAirspaceWarnings());
    return;
  }

  ActionInterface::SendMapSettings(true);
  PageActions::SaveCurrentPageAirspaceEnable();
}

void
InputEvents::eventAirspaceLabels(const char *misc)
{
  AirspaceRendererSettings &settings =
    CommonInterface::SetMapSettings().airspace;

  const bool currently_on =
    settings.label_selection == AirspaceRendererSettings::LabelSelection::ALL;

  if (StringIsEqual(misc, "toggle")) {
    settings.label_selection = currently_on
      ? AirspaceRendererSettings::LabelSelection::NONE
      : AirspaceRendererSettings::LabelSelection::ALL;
    Message::AddMessage(currently_on
                        ? _("Airspace labels hidden")
                        : _("Airspace labels shown"));
  } else if (StringIsEqual(misc, "off") || StringIsEqual(misc, "none")) {
    settings.label_selection = AirspaceRendererSettings::LabelSelection::NONE;
    Message::AddMessage(_("Airspace labels hidden"));
  } else if (StringIsEqual(misc, "on") || StringIsEqual(misc, "all")) {
    settings.label_selection = AirspaceRendererSettings::LabelSelection::ALL;
    Message::AddMessage(_("Airspace labels shown"));
  } else if (StringIsEqual(misc, "show")) {
    Message::AddMessage(currently_on
                        ? _("Airspace labels on")
                        : _("Airspace labels off"));
    return;
  } else
    return;

  Profile::Set(ProfileKeys::AirspaceLabelSelection,
               (int)settings.label_selection);
  ActionInterface::SendMapSettings(true);
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
