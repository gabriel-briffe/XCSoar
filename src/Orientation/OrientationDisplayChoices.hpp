// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Shared Map Display → Orientation choice lists (Config panel and page
 * overrides).
 */

#include "Form/DataField/Enum.hpp"
#include "Language/Language.hpp"
#include "MapSettings.hpp"

static constexpr StaticEnumChoice orientation_choices[] = {
  { MapOrientation::TRACK_UP, N_("Track up"),
    N_("The moving map display will be rotated so the glider's track is "
       "oriented up.") },
  { MapOrientation::HEADING_UP, N_("Heading up"),
    N_("The moving map display will be rotated so the glider's heading is "
       "oriented up.") },
  { MapOrientation::NORTH_UP, N_("North up"),
    N_("The moving map display will always be orientated north to south and "
       "the glider icon will be rotated to show its course.") },
  { MapOrientation::TARGET_UP, N_("Target up"),
    N_("The moving map display will be rotated so the navigation target is "
       "oriented up.") },
  { MapOrientation::WIND_UP, N_("Wind up"),
    N_("The moving map display will be rotated so the wind is always oriented "
       "up to down. (can be useful for wave flying)") },
  nullptr
};

static constexpr StaticEnumChoice map_shift_bias_choices[] = {
  { MapShiftBias::NONE, N_("None"), N_("Disable adjustments.") },
  { MapShiftBias::TRACK, N_("Track"),
    N_("Use a recent average of the ground track as basis.") },
  { MapShiftBias::TARGET, N_("Target"),
    N_("Use the current target waypoint as basis.") },
  nullptr
};
