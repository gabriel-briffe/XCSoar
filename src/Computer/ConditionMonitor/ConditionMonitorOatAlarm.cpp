// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "ConditionMonitorOatAlarm.hpp"
#include "Computer/Settings.hpp"
#include "NMEA/Info.hpp"
#include "Input/InputQueue.hpp"
#include "Language/Language.hpp"
#include "Message.hpp"
#include "Units/Units.hpp"
#include "util/StaticString.hxx"

bool
ConditionMonitorOatAlarm::CheckCondition(
  const NMEAInfo &basic,
  [[maybe_unused]] const DerivedInfo &calculated,
  const ComputerSettings &settings) noexcept
{
  last_oat = basic.temperature;
  last_threshold = settings.oat_alarm;
  return IsOatAlarmActive(settings.oat_alarm_enabled,
                          basic.temperature_available,
                          basic.temperature, settings.oat_alarm);
}

void
ConditionMonitorOatAlarm::Notify() noexcept
{
  StaticString<32> below, current;
  below.Format("%2.1f %s", last_threshold.ToUser(),
               Units::GetTemperatureName());
  current.Format("%2.1f %s", last_oat.ToUser(),
                 Units::GetTemperatureName());

  /* FindStatusMessage() uses this first string as the status-file
     key (delay 30 s).  The visible line is key + data. */
  StaticString<64> details;
  details.Format("%s: currently %s", below.c_str(), current.c_str());
  Message::AddMessage(_("OAT below"), details);

  InputEvents::processGlideComputer(GCE_OAT_ALARM);
}

TimeStamp
ConditionMonitorOatAlarm::GetCheckTime(const NMEAInfo &basic) const noexcept
{
  return basic.clock;
}
