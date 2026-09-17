// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeStatus.hpp"
#include "thread/Mutex.hxx"

namespace {
Mutex mutex;
GlideConeStatus::Snapshot current;
}

void
GlideConeStatus::Set(const Snapshot &snapshot) noexcept
{
  const std::lock_guard lock{mutex};
  if (current.valid == snapshot.valid &&
      (!snapshot.valid ||
       current.required_altitude == snapshot.required_altitude))
    return;
  current = snapshot;
}

void
GlideConeStatus::SetInvalid() noexcept
{
  const std::lock_guard lock{mutex};
  if (!current.valid)
    return;
  current = Snapshot{};
}

GlideConeStatus::Snapshot
GlideConeStatus::Get() noexcept
{
  const std::lock_guard lock{mutex};
  return current;
}
