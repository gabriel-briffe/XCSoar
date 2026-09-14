// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Thread-safe channel publishing the latest glide cone result at the
 * aircraft position.  Written by the draw thread (GlideConeRenderer) and
 * read by the UI thread (the Glide Cone InfoBox).
 */
namespace GlideConeStatus {

struct Snapshot {
  /** Whether a valid required altitude is available at the aircraft. */
  bool valid = false;

  /** Required arrival altitude at the aircraft position [m MSL]. */
  double required_altitude = 0;
};

void Set(const Snapshot &snapshot) noexcept;

void SetInvalid() noexcept;

[[gnu::pure]]
Snapshot Get() noexcept;

} // namespace GlideConeStatus
