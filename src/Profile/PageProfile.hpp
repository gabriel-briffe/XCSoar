// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettings.hpp"

class ProfileMap;

namespace Profile {
  void Load(const ProfileMap &map, PageSettings &settings);

  void Save(ProfileMap &map, const PageLayout &page, unsigned i);
  void Save(ProfileMap &map, const PageSettingOverrides &overrides,
            unsigned i);
  void Save(ProfileMap &map, const PageOnlyCommands &commands,
            unsigned i);
  void Save(ProfileMap &map, const PageZoomMemory &zoom,
            const PageOnlyCommands &commands, unsigned i);
  void Save(ProfileMap &map, const PageSettings &settings);
};
