// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettings.hpp"
#include "util/StaticString.hxx"

#include <cstdint>

namespace Rainbow {

inline constexpr time_t SNAPSHOT_INTERVAL_SECONDS = 600;
inline constexpr time_t SNAPSHOT_HISTORY_SECONDS = 2 * 3600;

/** Seconds after each 10-minute boundary when a new product is expected. */
inline constexpr time_t SNAPSHOT_PUBLISH_LAG_SECONDS = 120;

/** Retry delay after a failed Auto refresh while still connected. */
inline constexpr time_t AUTO_FAIL_RETRY_SECONDS = 120;

/** Poll interval while Auto is waiting for connectivity (no HTTP). */
inline constexpr time_t AUTO_CONNECTIVITY_POLL_SECONDS = 30;

/** Minimum interval between Rainbow snapshot API polls in Auto mode. */
inline constexpr time_t SNAPSHOT_POLL_MIN_SECONDS = 30;

void
ApplyCursorFromPageLayout(const PageLayout &layout) noexcept;

void
PersistCursorToPage() noexcept;

void
ActivatePageOverlay() noexcept;

void
ClearMapOverlay() noexcept;

void
Render() noexcept;

bool
StepTime(int delta) noexcept;

/**
 * Cycle layers: Sat → Rain → Sat+Rain → Sat …
 * @return true when the selection changed
 */
bool
StepLayer(int delta) noexcept;

void
FormatTimeLabel(StaticString<64> &text) noexcept;

void
FormatLayerLabel(StaticString<64> &text) noexcept;

/**
 * True while ActivatePageOverlay() has run (controls stay active even
 * before the first tiles finish downloading).  Pan-suspend of an
 * inactive Rainbow session must not count as active.
 */
bool
HasActiveOverlay() noexcept;

/**
 * Drop map overlay slots if Rainbow is not the active page overlay.
 * Available for recovery; Leave/ClearMapOverlay already no-ops when
 * inactive so pan leave of other providers does not need this.
 */
void
DiscardInactiveOverlays() noexcept;

[[nodiscard]] bool GetTimeAutoAdvance() noexcept;

void SetTimeAutoAdvance(bool auto_advance,
                        int64_t known_live_epoch = 0) noexcept;

/** Live Auto reference instant (map cursor, else latest floor). */
[[nodiscard]] int64_t GetLiveReferenceTime() noexcept;

/** Set cursor time and refresh the overlay (not Auto). */
[[nodiscard]] bool SetPageTime(int64_t time) noexcept;

/** Auto + enough history for a two-step replay. */
[[nodiscard]] bool CanReplayLiveTime() noexcept;

void
CancelHistoryPrefetch() noexcept;

} // namespace Rainbow
