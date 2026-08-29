// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettings.hpp"
#include "util/StaticString.hxx"

#include <cstdint>

namespace Rainbow {

inline constexpr time_t SNAPSHOT_INTERVAL_SECONDS = 600;
inline constexpr time_t SNAPSHOT_HISTORY_SECONDS = 2 * 3600;
inline constexpr time_t SNAPSHOT_PUBLISH_LAG_SECONDS = 120;
inline constexpr time_t AUTO_FAIL_RETRY_SECONDS = 120;
inline constexpr time_t AUTO_CONNECTIVITY_POLL_SECONDS = 30;
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
 * Cycle layers among modes checked in Weather → Rainbow:
 * Sat → Rain → Sat+Rain (skipping unchecked).
 */
bool
StepLayer(int delta) noexcept;

void
FormatTimeLabel(StaticString<64> &text) noexcept;

void
FormatLayerLabel(StaticString<64> &text) noexcept;

bool
HasActiveOverlay() noexcept;

void
DiscardInactiveOverlays() noexcept;

[[nodiscard]] bool GetTimeAutoAdvance() noexcept;

void SetTimeAutoAdvance(bool auto_advance,
                        int64_t known_live_epoch = 0) noexcept;

[[nodiscard]] int64_t GetLiveReferenceTime() noexcept;

[[nodiscard]] bool SetPageTime(int64_t time) noexcept;

[[nodiscard]] bool CanReplayLiveTime() noexcept;

void
CancelHistoryPrefetch() noexcept;

} // namespace Rainbow
