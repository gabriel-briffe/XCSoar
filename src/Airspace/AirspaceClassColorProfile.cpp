// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Airspace/AirspaceClassColorProfile.hpp"

#include "Look/AirspaceLook.hpp"
#include "Profile/AirspaceConfig.hpp"
#include "Profile/Current.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"
#include "util/StringFormat.hpp"

namespace AirspaceClassColorProfile {

void
InitPresetChoices(StaticEnumChoice *choices, char labels[][16]) noexcept
{
  for (unsigned i = 0; i < NUMAIRSPACECOLORS; ++i) {
    const RGB8Color color = AirspaceLook::preset_colors[i];
    StringFormat(labels[i], 16, "#%02X%02X%02X",
                 color.Red(), color.Green(), color.Blue());
    choices[i] = StaticEnumChoice(Pack(color), labels[i]);
  }

  choices[NUMAIRSPACECOLORS] = StaticEnumChoice(nullptr);
}

int
LoadFill(AirspaceClass cls) noexcept
{
  AirspaceClassRendererSettings settings;
  Profile::Load(Profile::map, unsigned(cls), settings);
  return Pack(settings.fill_color);
}

void
SaveFill(AirspaceClass cls, int packed) noexcept
{
  Profile::SetAirspaceFillColor(Profile::map, unsigned(cls), Unpack(packed));
}

int
LoadBorder(AirspaceClass cls) noexcept
{
  AirspaceClassRendererSettings settings;
  Profile::Load(Profile::map, unsigned(cls), settings);
  return Pack(settings.border_color);
}

void
SaveBorder(AirspaceClass cls, int packed) noexcept
{
  Profile::SetAirspaceBorderColor(Profile::map, unsigned(cls), Unpack(packed));
}

} // namespace AirspaceClassColorProfile
