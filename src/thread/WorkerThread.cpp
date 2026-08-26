// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "thread/WorkerThread.hpp"
#include "time/PeriodClock.hpp"

WorkerThread::WorkerThread(const char *_name,
                           Duration _period_min, Duration _idle_min,
                           Duration _delay) noexcept
  :SuspensibleThread(_name),
   period_min(_period_min),
   idle_min(_idle_min),
   delay(_delay)
{
}

void
WorkerThread::Run() noexcept
{
  PeriodClock clock;

  std::unique_lock lock{mutex};

  while (true) {
    /* wait for work */
    if (!trigger_flag) {
      if (_CheckStoppedOrSuspended(lock))
        break;

      /* check trigger_flag again to avoid a race condition, because
         _CheckStoppedOrSuspended() may have unlocked the mutex while
         we were suspended */
      if (!trigger_flag)
        trigger_cond.wait(lock);
    }

    /* Snapshot rate limits under the mutex (SetPeriods may change
       them from another thread). */
    const Duration current_delay = delay;
    const Duration current_period_min = period_min;
    const Duration current_idle_min = idle_min;

    /* got the "stop" trigger? */
    if (current_delay.count() > 0
        ? _WaitForStopped(lock, current_delay)
        : _CheckStoppedOrSuspended(lock))
      break;

    if (!trigger_flag)
      continue;

    trigger_flag = false;

    {
      const ScopeUnlock unlock(mutex);

      /* do the actual work */
      if (current_period_min.count() > 0)
        clock.Update();

      Tick();
    }

    auto idle = current_idle_min;
    if (current_period_min.count() > 0) {
      const auto elapsed = clock.Elapsed();
      if (elapsed + idle < current_period_min)
        idle = current_period_min - elapsed;
    }

    if (idle.count() > 0 && _WaitForStopped(lock, idle))
      break;
  }
}
