// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Settings.hpp"

void
GlideConeSettings::SetDefaults() noexcept
{
  enabled = false;
  glide_ratio = 40;
  max_altitude = 3000;
  iteration_cap = 2000;
}
