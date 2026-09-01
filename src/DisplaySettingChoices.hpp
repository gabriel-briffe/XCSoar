// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Shared on/off choice list for boolean display settings (page overrides
 * and config panels).
 */

#include "Form/DataField/Enum.hpp"
#include "Language/Language.hpp"

static constexpr StaticEnumChoice enabled_disabled_choices[] = {
  { 0, N_("Off") },
  { 1, N_("On") },
  nullptr
};
