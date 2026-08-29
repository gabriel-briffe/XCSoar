// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

class TopographyStore;

namespace TopographySettings {

/** Apply saved per-map layer thresholds to a loaded #TopographyStore. */
void
ApplyToStore(TopographyStore &store) noexcept;

/** Persist current layer thresholds for the configured map file. */
void
SaveFromStore(const TopographyStore &store) noexcept;

/**
 * Apply hard-coded layer thresholds for #ALPS_Test.xcm
 * (shape / label / important only).
 * @return number of layers updated (0 if the map is not ALPS_Test)
 */
unsigned
ApplyCustomPreset(TopographyStore &store) noexcept;

} // namespace TopographySettings
