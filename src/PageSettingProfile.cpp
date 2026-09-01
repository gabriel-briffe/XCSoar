// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSettingProfile.hpp"

#include "MapSettings.hpp"
#include "PageSettingDescriptor.hpp"
#include "Profile/Current.hpp"
#include "Profile/Profile.hpp"
#include "Terrain/TerrainDisplayChoices.hpp"
#include "Terrain/TerrainRenderer.hpp"
#include "Terrain/TerrainSettings.hpp"
#include "util/Compiler.h"
#include "util/Macros.hpp"

#include <cstdint>

namespace PageSettingProfile {

namespace {

using LoadFn = int (*)(const PageSettingDescriptor &) noexcept;
using SaveFn = void (*)(const PageSettingDescriptor &, int) noexcept;

struct WireHandler {
  LoadFn load;
  SaveFn save;
};

[[nodiscard]]
int
LoadBool(const PageSettingDescriptor &desc) noexcept
{
  bool value = desc.profile_default != 0;
  Profile::Get(desc.profile_key, value);
  return value ? 1 : 0;
}

void
SaveBool(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::Set(desc.profile_key, value != 0);
}

[[nodiscard]]
int
LoadUnsigned(const PageSettingDescriptor &desc) noexcept
{
  unsigned value = unsigned(desc.profile_default);
  if (!Profile::Get(desc.profile_key, value) ||
      value >= TerrainRendererSettings::NUM_RAMPS)
    value = unsigned(desc.profile_default);
  return int(value);
}

void
SaveUnsigned(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::Set(desc.profile_key, unsigned(value));
}

[[nodiscard]]
int
LoadUint8Slope(const PageSettingDescriptor &desc) noexcept
{
  uint8_t value = uint8_t(desc.profile_default);
  if (!Profile::Get(desc.profile_key, value) ||
      value >= uint8_t(SlopeShading::COUNT))
    value = uint8_t(desc.profile_default);
  return int(value);
}

void
SaveUint8Slope(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::SetEnum(desc.profile_key, SlopeShading(value));
}

[[nodiscard]]
int
LoadUint8Contours(const PageSettingDescriptor &desc) noexcept
{
  uint8_t value = uint8_t(desc.profile_default);
  if (!Profile::Get(desc.profile_key, value) ||
      value >= uint8_t(Contours::COUNT))
    value = uint8_t(desc.profile_default);
  return int(value);
}

void
SaveUint8Contours(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::SetEnum(desc.profile_key, Contours(value));
}

[[nodiscard]]
int
LoadShortPercent(const PageSettingDescriptor &desc) noexcept
{
  short value = short(desc.profile_default);
  Profile::Get(desc.profile_key, value);
  return TerrainByteToPercent(value);
}

void
SaveShortPercent(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::Set(desc.profile_key, TerrainPercentToByte(short(value)));
}

[[nodiscard]]
int
LoadUint8MapOrientation(const PageSettingDescriptor &desc) noexcept
{
  unsigned value = unsigned(desc.profile_default);
  if (!Profile::Get(desc.profile_key, value) ||
      !IsValidMapOrientation(value))
    value = unsigned(desc.profile_default);
  return int(value);
}

void
SaveUint8MapOrientation(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::Set(desc.profile_key, unsigned(value));
}

[[nodiscard]]
int
LoadUint8MapShiftBias(const PageSettingDescriptor &desc) noexcept
{
  MapShiftBias value = MapShiftBias(desc.profile_default);
  if (!Profile::GetEnum(desc.profile_key, value))
    value = MapShiftBias(desc.profile_default);
  return int(value);
}

void
SaveUint8MapShiftBias(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::SetEnum(desc.profile_key, MapShiftBias(value));
}

[[nodiscard]]
int
LoadInt(const PageSettingDescriptor &desc) noexcept
{
  int value = desc.profile_default;
  Profile::Get(desc.profile_key, value);
  return value;
}

void
SaveInt(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::Set(desc.profile_key, value);
}

static constexpr WireHandler handlers[] = {
  { LoadBool, SaveBool },
  { LoadUnsigned, SaveUnsigned },
  { LoadUint8Slope, SaveUint8Slope },
  { LoadUint8Contours, SaveUint8Contours },
  { LoadShortPercent, SaveShortPercent },
  { LoadUint8MapOrientation, SaveUint8MapOrientation },
  { LoadUint8MapShiftBias, SaveUint8MapShiftBias },
  { LoadInt, SaveInt },
};

static_assert(ARRAY_SIZE(handlers) == unsigned(ProfileWireFormat::COUNT),
              "Wire handlers must match ProfileWireFormat::COUNT");

} // namespace

int
Load(const PageSettingDescriptor &desc) noexcept
{
  return handlers[unsigned(desc.profile_wire)].load(desc);
}

void
Save(const PageSettingDescriptor &desc, int value) noexcept
{
  handlers[unsigned(desc.profile_wire)].save(desc, value);
}

} // namespace PageSettingProfile
