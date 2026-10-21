// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "ConditionMonitor.hpp"
#include "Atmosphere/Temperature.hpp"

/**
 * True when the OAT alarm should be active: the setting is on, a
 * probe value is present, and it is at or below the threshold.
 */
[[nodiscard]] [[gnu::const]]
constexpr bool
IsOatAlarmActive(bool enabled, bool available,
                 Temperature oat, Temperature threshold) noexcept
{
  return enabled && available && oat <= threshold;
}

/**
 * Warn if outside air temperature is at or below the configured
 * threshold, on the ground or in flight.
 */
class ConditionMonitorOatAlarm final : public ConditionMonitor {
  Temperature last_oat = Temperature::FromCelsius(0);
  Temperature last_threshold = Temperature::FromCelsius(0);

public:
  constexpr ConditionMonitorOatAlarm() noexcept
    :ConditionMonitor(std::chrono::minutes{5}, std::chrono::seconds{1},
                      false) {}

protected:
  bool CheckCondition(const NMEAInfo &basic,
                      const DerivedInfo &calculated,
                      const ComputerSettings &settings) noexcept override;
  void Notify() noexcept override;
  void SaveLast() noexcept override {}
  TimeStamp GetCheckTime(const NMEAInfo &basic) const noexcept override;
  bool RearmWhenClears() const noexcept override {
    return true;
  }
};
