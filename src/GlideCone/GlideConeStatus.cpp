// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeStatus.hpp"
#include "thread/Mutex.hxx"

namespace {
Mutex mutex;
GlideConeStatus::Snapshot current;
unsigned last_compute_ms = 0;
bool has_compute_ms = false;
}

void
GlideConeStatus::Set(const Snapshot &snapshot) noexcept
{
  const std::lock_guard lock{mutex};
  if (current.valid == snapshot.valid &&
      (!snapshot.valid ||
       (current.ridge_soaring_proof_altitude ==
          snapshot.ridge_soaring_proof_altitude &&
        current.path_distance == snapshot.path_distance &&
        current.destination_waypoint_id ==
          snapshot.destination_waypoint_id)))
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

void
GlideConeStatus::NoteLastComputeMs(unsigned ms) noexcept
{
  const std::lock_guard lock{mutex};
  last_compute_ms = ms;
  has_compute_ms = true;
}

std::optional<unsigned>
GlideConeStatus::LastComputeMs() noexcept
{
  const std::lock_guard lock{mutex};
  if (!has_compute_ms)
    return std::nullopt;
  return last_compute_ms;
}
