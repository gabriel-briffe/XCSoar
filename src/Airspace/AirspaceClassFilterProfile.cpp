// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Airspace/AirspaceClassFilterProfile.hpp"

#include "Profile/AirspaceConfig.hpp"
#include "Profile/Current.hpp"
#include "Profile/Profile.hpp"
#include "util/StringFormat.hpp"

namespace AirspaceClassFilterProfile {

int
Load(AirspaceClass cls) noexcept
{
  bool display = true;
  bool warn = true;

  char name[64];
  StringFormat(name, sizeof(name), "AirspaceDisplay%u", unsigned(cls));
  if (!Profile::Get(name, display)) {
    unsigned legacy = 0;
    StringFormat(name, sizeof(name), "AirspaceMode%u", unsigned(cls));
    if (Profile::Get(name, legacy))
      display = (legacy & 0x1) != 0;
  }

  StringFormat(name, sizeof(name), "AirspaceWarning%u", unsigned(cls));
  if (!Profile::Get(name, warn)) {
    unsigned legacy = 0;
    StringFormat(name, sizeof(name), "AirspaceMode%u", unsigned(cls));
    if (Profile::Get(name, legacy))
      warn = (legacy & 0x2) != 0;
  }

  return Encode(display, warn);
}

void
Save(AirspaceClass cls, int mode) noexcept
{
  Profile::SetAirspaceMode(Profile::map, unsigned(cls),
                           Display(mode), Warn(mode));
}

} // namespace AirspaceClassFilterProfile
