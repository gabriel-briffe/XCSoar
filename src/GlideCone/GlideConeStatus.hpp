// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include <optional>

/**
 * Thread-safe channel publishing the latest glide cone result at the
 * aircraft position.  Written by the draw thread (GlideConeRenderer) and
 * read by the UI thread (Glide Cone InfoBoxes).
 */
namespace GlideConeStatus {

struct Snapshot {
  /** Whether a valid ridge-soaring-proof altitude / path is available. */
  bool valid = false;

  /**
   * Ridge-soaring-proof GlideCone altitude at the aircraft [m MSL]
   * (InfoBox margin); not the raw stored field altitude on ground.
   */
  double ridge_soaring_proof_altitude = 0;

  /**
   * Euclidean ground distance [m] along the relay path from the
   * aircraft to the seed.  Zero when unknown.
   */
  double path_distance = 0;

  /**
   * Waypoint id of the landable at the end of the relay path
   * (0 if unknown).
   */
  unsigned destination_waypoint_id = 0;
};

void Set(const Snapshot &snapshot) noexcept;

void SetInvalid() noexcept;

[[gnu::pure]]
Snapshot Get() noexcept;

/** Record wall time of the last successful cone install [ms]. */
void NoteLastComputeMs(unsigned ms) noexcept;

[[gnu::pure]]
std::optional<unsigned> LastComputeMs() noexcept;

} // namespace GlideConeStatus
