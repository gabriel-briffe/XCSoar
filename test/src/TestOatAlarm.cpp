// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Computer/ConditionMonitor/ConditionMonitorOatAlarm.hpp"
#include "TestUtil.hpp"

int main()
{
  plan_tests(5);

  const auto threshold = Temperature::FromCelsius(-35);

  ok1(!IsOatAlarmActive(false, true,
                        Temperature::FromCelsius(-40), threshold));
  ok1(!IsOatAlarmActive(true, false,
                        Temperature::FromCelsius(-40), threshold));
  ok1(!IsOatAlarmActive(true, true,
                        Temperature::FromCelsius(-34), threshold));
  ok1(IsOatAlarmActive(true, true,
                       Temperature::FromCelsius(-35), threshold));
  ok1(IsOatAlarmActive(true, true,
                       Temperature::FromCelsius(-40), threshold));

  return exit_status();
}
