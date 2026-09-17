// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Thread-safe channel publishing the latest glide cone result at the
 * aircraft position.  Written by the draw thread (GlideConeRenderer) and
 * read by the UI thread (Glide Cone InfoBoxes).
 */
namespace GlideConeStatus {

struct Snapshot {
  /** Whether a valid required altitude / path is available. */
  bool valid = false;

  /** Required arrival altitude at the aircraft position [m MSL]. */
  double required_altitude = 0;

  /**
   * Euclidean ground distance [m] along the relay path from the
   * aircraft to the seed.  Zero when unknown.
   */
  double path_distance = 0;
};

void Set(const Snapshot &snapshot) noexcept;

void SetInvalid() noexcept;

[[gnu::pure]]
Snapshot Get() noexcept;

} // namespace GlideConeStatus
