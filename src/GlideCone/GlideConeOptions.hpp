// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Geo/GeoBounds.hpp"
#include "ui/dim/Rect.hpp"

class Font;

#include <cstdint>
#include <optional>
#include <vector>

struct GlideConeField;
struct GlideConeSettings;
class Canvas;
class WindowProjection;

/**
 * Optional-area mask: cells reachable downhill from the aircraft while
 * staying above ground clearance and the airport cone.  Arrival uses a
 * straight line to the last visible origin, matching the GPU shader.
 */
namespace GlideConeOptions {

void RequestOnce() noexcept;
void Clear() noexcept;

[[gnu::pure]]
std::optional<unsigned> LastComputeMs() noexcept;

void Update(const GlideConeField &field, double start_alt,
            int gi, int gj, const GlideConeSettings &settings) noexcept;

void Draw(Canvas &canvas, const WindowProjection &projection,
          const GlideConeSettings &settings) noexcept;

void DrawTimer(Canvas &canvas, const PixelRect &rc, const Font &font,
               const GlideConeSettings &settings) noexcept;
bool HitTimer(PixelPoint p) noexcept;
void ResetTimer() noexcept;

} // namespace GlideConeOptions
