// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Airspace/AirspaceDisplayChoices.hpp"
#include "Engine/Airspace/AirspaceClass.hpp"

#include <cstdint>

/**
 * Profile keys for per-class airspace display/warning filters
 * (#AirspaceDisplay%u, #AirspaceWarning%u, legacy #AirspaceMode%u).
 */
namespace AirspaceClassFilterProfile {

[[nodiscard]]
constexpr int
Encode(bool display, bool warn) noexcept
{
  return (display ? int(AirspaceClassFilterMode::DISPLAY) : 0) |
         (warn ? int(AirspaceClassFilterMode::WARN) : 0);
}

[[nodiscard]]
constexpr bool
Display(int mode) noexcept
{
  return (mode & int(AirspaceClassFilterMode::DISPLAY)) != 0;
}

[[nodiscard]]
constexpr bool
Warn(int mode) noexcept
{
  return (mode & int(AirspaceClassFilterMode::WARN)) != 0;
}

[[nodiscard]]
int
Load(AirspaceClass cls) noexcept;

void
Save(AirspaceClass cls, int mode) noexcept;

} // namespace AirspaceClassFilterProfile
