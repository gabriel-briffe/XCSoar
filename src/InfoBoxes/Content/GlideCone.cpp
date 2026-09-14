// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideCone.hpp"
#include "InfoBoxes/Data.hpp"
#include "InfoBoxes/Panel/Panel.hpp"
#include "InfoBoxes/Panel/GlideConeMode.hpp"
#include "InfoBoxes/Panel/GlideConeSetup.hpp"
#include "Interface.hpp"
#include "Computer/Settings.hpp"
#include "NMEA/MoreData.hpp"
#include "GlideCone/GlideConeStatus.hpp"
#include "Formatter/UserUnits.hpp"
#include "Language/Language.hpp"
#include "util/StaticString.hxx"

/*
 * Title: "GC L/D <ratio>".  Main value: altitude margin (glider altitude
 * minus the altitude required by the glide cone computation to reach the
 * Goto airport / nearest landable); green when >=0, red when below.
 * Comment: the required altitude at the aircraft position.
 *
 * The delta sign convention and green/red colouring follow the original
 * gpu-MC glide cone code.
 */

static constexpr InfoBoxPanel panels[] = {
  { N_("Mode"), LoadGlideConeModePanel },
  { NC_("Menu", "Setup"), LoadGlideConeSetupPanel },
  { nullptr, nullptr },
};

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

  const double required = status.required_altitude;
  const double delta = basic.nav_altitude - required;

  data.SetValueFromArrival(delta);
  data.SetComment(FormatUserAltitude(required).c_str());

  // green when at/above required altitude, red when below
  data.SetValueColor(delta >= 0 ? 3 : 1);
}
