// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Weather/MapOverlay/FieldPickerResult.hpp"
#include "util/StaticString.hxx"

struct PageLayout;

namespace SkySight {

using LayerPickerResult = WeatherMapOverlay::FieldPickerResult;

void ApplyCursorFromPageLayout(const PageLayout &page) noexcept;

[[nodiscard]]
bool EditTimeOnLayout(PageLayout &page) noexcept;

void OpenTimePicker() noexcept;

[[nodiscard]]
LayerPickerResult EditLayerOnLayout(PageLayout &page,
                                    bool offer_setup=false) noexcept;

[[nodiscard]]
LayerPickerResult OpenLayerPicker(bool offer_setup=false) noexcept;

void FormatTimeLabelForPage(StaticString<64> &text,
                            const PageLayout &page) noexcept;

void FormatLayerLabelForPage(StaticString<64> &text,
                             const PageLayout &page) noexcept;

[[nodiscard]] bool IsTimeSelectable(const PageLayout &page) noexcept;
[[nodiscard]] bool IsTimeSelectable() noexcept;
[[nodiscard]] bool HasSelectedLayer() noexcept;
[[nodiscard]] bool HasSelectedTimeData() noexcept;

[[nodiscard]] bool StepTime(int delta) noexcept;
[[nodiscard]] bool StepLayer(int delta) noexcept;

[[nodiscard]] bool GetTimeAutoAdvance() noexcept;
void SetTimeAutoAdvance(bool auto_advance) noexcept;
void ApplyAutoAdvanceTime() noexcept;
void EnableTimeAutoFromInput() noexcept;

/** Live Auto reference instant (map cursor, else latest floor). */
[[nodiscard]] time_t GetLiveReferenceTime(const PageLayout &page) noexcept;

/** Set page time and refresh the live overlay (not Auto). */
[[nodiscard]] bool SetPageTime(int64_t time) noexcept;

/** Auto + live layer with two steppable slots in history. */
[[nodiscard]] bool CanReplayLiveTime() noexcept;

} // namespace SkySight
