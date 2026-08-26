// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "util/StaticString.hxx"

struct RainbowSettings {
  /** Rainbow.ai API token from the developer portal. */
  StaticString<128> api_key;

  /** Persist checkbox selection from the Weather → Rainbow tab. */
  bool display_satellite;
  bool display_rain;

  constexpr bool IsDefined() const noexcept {
    return !api_key.empty();
  }

  void SetDefaults() noexcept {
    api_key.clear();
    display_satellite = true;
    display_rain = false;
  }
};
