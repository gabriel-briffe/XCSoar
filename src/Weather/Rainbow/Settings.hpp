// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "util/StaticString.hxx"

struct RainbowSettings {
  StaticString<128> api_key;

  /** Which layer modes the weather-control cycle may show. */
  bool display_satellite;
  bool display_rain;
  bool display_sat_rain;

  constexpr bool IsDefined() const noexcept {
    return !api_key.empty();
  }

  void SetDefaults() noexcept {
    api_key.clear();
    display_satellite = true;
    display_rain = false;
    display_sat_rain = false;
  }
};
