// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Airspace/AirspaceClassColorProfile.hpp"

#include "Profile/AirspaceConfig.hpp"
#include "Profile/Current.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"

#include <cassert>

namespace AirspaceClassColorProfile {

namespace {

[[nodiscard]]
AirspaceClassRendererSettings
LoadClassSettings(AirspaceClass cls) noexcept
{
  assert(unsigned(cls) < AIRSPACECLASSCOUNT);

  /* Same order as MapSettings airspace load: per-class defaults, then
     profile overlay.  Without defaults, a missing AirspaceFillColorN
     leaves RGB8Color uninitialized and page baseline apply can paint
     every class with garbage (often seen as magenta/pink). */
  AirspaceRendererSettings defaults;
  defaults.SetDefaults();
  AirspaceClassRendererSettings settings = defaults.classes[unsigned(cls)];
  Profile::Load(Profile::map, unsigned(cls), settings);
  return settings;
}

} // namespace

int
LoadFill(AirspaceClass cls) noexcept
{
  return Pack(LoadClassSettings(cls).fill_color);
}

void
SaveFill(AirspaceClass cls, int packed) noexcept
{
  Profile::SetAirspaceFillColor(Profile::map, unsigned(cls), Unpack(packed));
}

int
LoadBorder(AirspaceClass cls) noexcept
{
  return Pack(LoadClassSettings(cls).border_color);
}

void
SaveBorder(AirspaceClass cls, int packed) noexcept
{
  Profile::SetAirspaceBorderColor(Profile::map, unsigned(cls), Unpack(packed));
}

int
LoadBorderWidth(AirspaceClass cls) noexcept
{
  return int(LoadClassSettings(cls).border_width);
}

void
SaveBorderWidth(AirspaceClass cls, int width) noexcept
{
  assert(width >= 0);
  Profile::SetAirspaceBorderWidth(Profile::map, unsigned(cls),
                                  unsigned(width));
}

int
LoadFillMode(AirspaceClass cls) noexcept
{
  return int(LoadClassSettings(cls).fill_mode);
}

void
SaveFillMode(AirspaceClass cls, int mode) noexcept
{
  Profile::SetAirspaceFillMode(Profile::map, unsigned(cls), uint8_t(mode));
}

} // namespace AirspaceClassColorProfile
