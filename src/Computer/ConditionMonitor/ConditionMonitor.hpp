// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "time/Stamp.hpp"

struct NMEAInfo;
struct DerivedInfo;
struct ComputerSettings;

/**
 * Base class for system to monitor changes in state and issue
 * warnings or informational messages based on various conditions.
 */
class ConditionMonitor
{
protected:
  TimeStamp LastTime_Notification = TimeStamp::Undefined();
  TimeStamp LastTime_Check = TimeStamp::Undefined();
  FloatDuration Interval_Notification;
  const FloatDuration Interval_Check;
  const bool require_flying;

public:
  constexpr ConditionMonitor(FloatDuration _interval_notification,
                             FloatDuration _interval_check,
                             bool _require_flying=true) noexcept
    :Interval_Notification(_interval_notification),
     Interval_Check(_interval_check),
     require_flying(_require_flying) {}

  void Update(const NMEAInfo &basic, const DerivedInfo &calculated,
              const ComputerSettings &settings) noexcept;

private:
  virtual bool CheckCondition(const NMEAInfo &basic,
                              const DerivedInfo &calculated,
                              const ComputerSettings &settings) noexcept = 0;
  virtual void Notify() noexcept = 0;
  virtual void SaveLast() noexcept = 0;

  /**
   * When the condition is false, forget the last notification so the
   * next time it becomes true it warns at once.  Other monitors keep
   * the notification interval across gaps.
   */
  virtual bool RearmWhenClears() const noexcept {
    return false;
  }

  bool Ready_Time_Notification(TimeStamp t) const noexcept;
  bool Ready_Time_Check(TimeStamp t, bool *restart) noexcept;

protected:
  /**
   * Time stamp used for the check and notification intervals.
   * Defaults to GPS time; a monitor that must work without a GPS fix
   * (for example OAT) can use #NMEAInfo::clock instead.
   */
  virtual TimeStamp GetCheckTime(const NMEAInfo &basic) const noexcept;
};
