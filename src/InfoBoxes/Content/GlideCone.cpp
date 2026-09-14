// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideCone.hpp"
#include "InfoBoxes/Data.hpp"
#include "Interface.hpp"
#include "NMEA/MoreData.hpp"
#include "GlideCone/GlideConeStatus.hpp"
#include "Formatter/UserUnits.hpp"

/*
 * Main value: the altitude margin at the aircraft, i.e. the glider's
 * current altitude minus the altitude required (by the glide cone
 * computation) to still reach the "Goto" airport.  Positive (green)
 * means reachable with margin, negative (red) means below the cone.
 * Comment: the required altitude at the aircraft position.
 *
 * The delta sign convention and green/red colouring follow the original
 * gpu-MC glide cone code.
 */
void
UpdateInfoBoxGlideCone(InfoBoxData &data) noexcept
{
  const auto status = GlideConeStatus::Get();
  const MoreData &basic = CommonInterface::Basic();

  if (!status.valid || !basic.NavAltitudeAvailable()) {
    data.SetInvalid();
    return;
  }

  const double required = status.required_altitude;
  const double delta = basic.nav_altitude - required;

  data.SetValueFromArrival(delta);
  data.SetComment(FormatUserAltitude(required).c_str());

  // green when at/above required altitude, red when below
  data.SetValueColor(delta >= 0 ? 3 : 1);
}
