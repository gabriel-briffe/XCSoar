// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Engine/Airspace/AirspaceClass.hpp"
#include "ui/canvas/PortableColor.hpp"

/**
 * Packed RGB profile values for per-page airspace class colours
 * (#AirspaceFillColor%u, #AirspaceBorderColor%u).
 */
namespace AirspaceClassColorProfile {

[[nodiscard]]
constexpr int
Pack(const RGB8Color &color) noexcept
{
  return (int(color.Red()) << 16) |
         (int(color.Green()) << 8) |
         int(color.Blue());
}

[[nodiscard]]
constexpr RGB8Color
Unpack(int packed) noexcept
{
  return RGB8Color(uint8_t((packed >> 16) & 0xff),
                   uint8_t((packed >> 8) & 0xff),
                   uint8_t(packed & 0xff));
}

[[nodiscard]]
constexpr bool
IsValid(int packed) noexcept
{
  return packed >= 0 && packed <= 0xffffff;
}

[[nodiscard]]
int
LoadFill(AirspaceClass cls) noexcept;

void
SaveFill(AirspaceClass cls, int packed) noexcept;

[[nodiscard]]
int
LoadBorder(AirspaceClass cls) noexcept;

void
SaveBorder(AirspaceClass cls, int packed) noexcept;

[[nodiscard]]
int
LoadBorderWidth(AirspaceClass cls) noexcept;

void
SaveBorderWidth(AirspaceClass cls, int width) noexcept;

[[nodiscard]]
int
LoadFillMode(AirspaceClass cls) noexcept;

void
SaveFillMode(AirspaceClass cls, int mode) noexcept;

} // namespace AirspaceClassColorProfile
