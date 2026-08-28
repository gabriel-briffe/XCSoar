// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Manufacturer-specific IGC E-record codes written by XCSoar for
 * FlyingComputer takeoff and landing detection.  The Logbook dialog
 * uses these markers exclusively.
 */
namespace IGCFlightEvent {

constexpr const char TAKEOFF[] = "TKOFF";
constexpr const char LANDING[] = "LAND";

} // namespace IGCFlightEvent
