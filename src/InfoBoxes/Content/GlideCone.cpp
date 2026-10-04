// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideCone.hpp"
#include "InfoBoxes/Data.hpp"
#include "InfoBoxes/Panel/Panel.hpp"
#include "InfoBoxes/Panel/GlideConeSetup.hpp"
#include "Interface.hpp"
#include "Computer/Settings.hpp"
#include "NMEA/MoreData.hpp"
#include "GlideCone/GlideConeStatus.hpp"
#include "Formatter/UserUnits.hpp"
#include "Language/Language.hpp"
#include "Components.hpp"
#include "DataComponents.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
#include "util/StaticString.hxx"
#include "util/TruncateString.hpp"

#include <cstring>

/*
 * Title: "GC L/D <ratio>".  Main value: altitude margin (glider altitude
 * minus RidgeSoaringProofGlideConeAltitude); green when >=0, red when
 * below.  Comment: that same proof altitude.  On a ground cell the
 * stored field value is terrain, so the proof walks to the first air
 * cell on the relay path plus distance / L/D — so the InfoBox does not
 * show a large negative margin on the ridge (clear of terrain you would
 * see a huge positive vs stored height).  While a new cone is computed
 * the last proof altitude is kept (the map path likewise).
 *
 * The delta sign convention and green/red colouring follow the original
 * gpu-MC glide cone code.
 */

static constexpr InfoBoxPanel panels[] = {
  { NC_("Menu", "Setup"), LoadGlideConeSetupPanel },
  { NC_("Menu", "Contours"), LoadGlideConeContoursPanel },
  { nullptr, nullptr },
};

/**
 * Format a waypoint label using the map Waypoints "Label format"
 * setting (#WaypointRendererSettings::display_text_type).
 */
static void
FormatWaypointLabel(char *buffer, std::size_t buffer_size,
                    const Waypoint &waypoint,
                    WaypointRendererSettings::DisplayTextType type) noexcept
{
  buffer[0] = '\0';

  switch (type) {
  case WaypointRendererSettings::DisplayTextType::NAME:
    CopyTruncateString(buffer, buffer_size, waypoint.name.c_str());
    break;

  case WaypointRendererSettings::DisplayTextType::FIRST_FIVE:
    CopyTruncateString(buffer, buffer_size, waypoint.name.c_str(), 5);
    break;

  case WaypointRendererSettings::DisplayTextType::FIRST_THREE:
    CopyTruncateString(buffer, buffer_size, waypoint.name.c_str(), 3);
    break;

  case WaypointRendererSettings::DisplayTextType::NONE:
    break;

  case WaypointRendererSettings::DisplayTextType::FIRST_WORD:
    CopyTruncateString(buffer, buffer_size, waypoint.name.c_str());
    if (char *tmp = strstr(buffer, " "); tmp != nullptr)
      *tmp = '\0';
    break;

  case WaypointRendererSettings::DisplayTextType::SHORT_NAME:
    if (!waypoint.shortname.empty())
      CopyTruncateString(buffer, buffer_size, waypoint.shortname.c_str());
    else
      CopyTruncateString(buffer, buffer_size, waypoint.name.c_str(), 5);
    break;

  case WaypointRendererSettings::DisplayTextType::OBSOLETE_DONT_USE_NUMBER:
  case WaypointRendererSettings::DisplayTextType::OBSOLETE_DONT_USE_NAMEIFINTASK:
    CopyTruncateString(buffer, buffer_size, waypoint.name.c_str());
    break;
  }
}

const InfoBoxPanel *
InfoBoxContentGlideCone::GetDialogContent() noexcept
{
  return panels;
}

void
InfoBoxContentGlideCone::Update(InfoBoxData &data) noexcept
{
  const GlideConeSettings &gc =
    CommonInterface::GetComputerSettings().glide_cone;

  StaticString<32> title;
  title.Format("GC L/D %d", int(gc.glide_ratio + 0.5));
  data.SetTitle(title.c_str());

  const auto status = GlideConeStatus::Get();
  const MoreData &basic = CommonInterface::Basic();

  if (!status.valid || !basic.NavAltitudeAvailable()) {
    data.SetValueInvalid();
    data.SetCommentInvalid();
    return;
  }

  const double proof = status.ridge_soaring_proof_altitude;
  const double delta = basic.nav_altitude - proof;

  data.SetValueFromArrival(delta);
  data.SetComment(FormatUserAltitude(proof).c_str());

  // green when at/above proof altitude, red when below
  data.SetValueColor(delta >= 0 ? 3 : 1);
}

const InfoBoxPanel *
InfoBoxContentGlideConeDist::GetDialogContent() noexcept
{
  return panels;
}

void
InfoBoxContentGlideConeDist::Update(InfoBoxData &data) noexcept
{
  data.SetTitle(_("GC Dist"));

  const auto status = GlideConeStatus::Get();
  if (!status.valid || status.path_distance <= 0) {
    data.SetInvalid();
    return;
  }

  data.SetValueFromDistance(status.path_distance);

  if (status.destination_waypoint_id == 0 ||
      data_components == nullptr ||
      data_components->waypoints == nullptr) {
    data.SetCommentInvalid();
    return;
  }

  const auto wp =
    data_components->waypoints->LookupId(status.destination_waypoint_id);
  if (wp == nullptr) {
    data.SetCommentInvalid();
    return;
  }

  const auto text_type =
    CommonInterface::GetMapSettings().waypoint.display_text_type;
  char label[32];
  FormatWaypointLabel(label, sizeof(label), *wp, text_type);
  if (label[0] == '\0')
    data.SetCommentInvalid();
  else
    data.SetComment(label);
}
