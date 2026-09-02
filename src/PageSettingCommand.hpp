// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSetting.hpp"

/**
 * Display / Quick-menu commands that may be marked page-only.
 */
struct PageSettingCommandDescriptor {
  PageSettingId id;
  const char *label;
};

[[nodiscard]]
unsigned
PageSettingCommandCount() noexcept;

[[nodiscard]]
const PageSettingCommandDescriptor &
PageSettingCommandGet(unsigned index) noexcept;

[[nodiscard]]
bool
PageSettingCommandIsKnown(PageSettingId id) noexcept;

[[nodiscard]]
const PageSettingCommandDescriptor *
PageSettingCommandFind(PageSettingId id) noexcept;
