// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Waypoints/WaypointMapFilterTypes.hpp"

#include <cstddef>

struct WaypointRendererSettings;

/**
 * Profile keys for map waypoint type filters (#WaypointTypeDisplay%u).
 */
namespace WaypointMapFilterProfile {

void
FormatTypeDisplayKey(char *buffer, size_t size,
                     unsigned type_index) noexcept;

[[nodiscard]]
bool
LoadTypeDisplay(unsigned type_index) noexcept;

void
SaveTypeDisplay(unsigned type_index, bool display) noexcept;

void
Load(WaypointRendererSettings &settings) noexcept;

} // namespace WaypointMapFilterProfile
