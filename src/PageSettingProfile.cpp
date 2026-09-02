// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSettingProfile.hpp"

#include "PageSettingCatalog.hpp"
#include "PageSettingDescriptor.hpp"
#include "Profile/Current.hpp"
#include "Profile/Profile.hpp"
#include "Terrain/TerrainDisplayChoices.hpp"
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
      !PageSettingCatalog::IsValidValue(desc, int(value)))
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

[[nodiscard]]
int
LoadUint8Enum(const PageSettingDescriptor &desc) noexcept
{
  unsigned value = unsigned(desc.profile_default);
  if (!Profile::Get(desc.profile_key, value) ||
      !PageSettingCatalog::IsValidValue(desc, int(value)))
    value = unsigned(desc.profile_default);
  return int(value);
}

void
SaveUint8Enum(const PageSettingDescriptor &desc, int value) noexcept
{
  Profile::Set(desc.profile_key, unsigned(value));
}

static constexpr WireHandler handlers[] = {
  { LoadBool, SaveBool },
  { LoadUnsigned, SaveUnsigned },
  { LoadShortPercent, SaveShortPercent },
  { LoadInt, SaveInt },
  { LoadUint8Enum, SaveUint8Enum },
};

static_assert(ARRAY_SIZE(handlers) == unsigned(ProfileWireFormat::COUNT),
              "Wire handlers must match ProfileWireFormat::COUNT");

} // namespace

int
Load(const PageSettingDescriptor &desc) noexcept
{
  if (desc.profile_key.empty())
    return desc.profile_default;

  return handlers[unsigned(desc.profile_wire)].load(desc);
}

void
Save(const PageSettingDescriptor &desc, int value) noexcept
{
  if (desc.profile_key.empty())
    return;

  handlers[unsigned(desc.profile_wire)].save(desc, value);
}

} // namespace PageSettingProfile
