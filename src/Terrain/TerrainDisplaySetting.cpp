// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TerrainDisplaySetting.hpp"

#include "Interface.hpp"
#include "MapSettings.hpp"
#include "PageSetting.hpp"
#include "Profile/Current.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Profile/TerrainConfig.hpp"
#include "Terrain/TerrainDisplayChoices.hpp"
#include "Terrain/TerrainRenderer.hpp"
#include "util/Macros.hpp"

#include <cassert>
#include <cstdint>

namespace TerrainDisplaySetting {

namespace {

static constexpr PageSettingDescriptor catalog[] = {
  {
    PageSettingId::TERRAIN_ENABLE,
    PageSettingType::BOOL,
    N_("Terrain Display"),
    N_("Draw a digital elevation terrain on the map."),
    "OverrideTerrainEnable",
    ProfileKeys::DrawTerrain,
    TerrainBundleField::TERRAIN_ENABLE,
    ProfileWireFormat::BOOL,
    1,
    terrain_enable_choices,
    0, 0, 0,
  },
  {
    PageSettingId::TOPOGRAPHY_ENABLE,
    PageSettingType::BOOL,
    N_("Topography display"),
    N_("Draw topographical features (roads, rivers, lakes etc.) on the map."),
    "OverrideTopographyEnable",
    ProfileKeys::DrawTopography,
    TerrainBundleField::TOPOGRAPHY_ENABLE,
    ProfileWireFormat::BOOL,
    1,
    terrain_enable_choices,
    0, 0, 0,
  },
  {
    PageSettingId::TERRAIN_COLORS,
    PageSettingType::ENUM,
    N_("Terrain colors"),
    N_("Defines the color ramp used in terrain rendering."),
    "OverrideTerrainColors",
    ProfileKeys::TerrainRamp,
    TerrainBundleField::TERRAIN_RAMP,
    ProfileWireFormat::UNSIGNED,
    0,
    terrain_ramp_choices,
    0, 0, 0,
  },
  {
    PageSettingId::TERRAIN_SLOPE_SHADING,
    PageSettingType::ENUM,
    N_("Slope shading"),
    N_("The terrain can be shaded among slopes to indicate either "
       "wind direction, sun position, a geographically fixed shading from "
       "North-West, or a screen-relative fixed shading from top left."),
    "OverrideTerrainSlopeShading",
    ProfileKeys::SlopeShadingType,
    TerrainBundleField::TERRAIN_SLOPE_SHADING,
    ProfileWireFormat::UINT8_SLOPE,
    int(SlopeShading::FIXED),
    terrain_slope_shading_choices,
    0, 0, 0,
  },
  {
    PageSettingId::TERRAIN_CONTRAST,
    PageSettingType::INTEGER,
    N_("Terrain contrast"),
    N_("Defines the amount of Phong shading in the terrain rendering. "
       "Use large values to emphasise terrain slope, smaller values if "
       "flying in steep mountains."),
    "OverrideTerrainContrast",
    ProfileKeys::TerrainContrast,
    TerrainBundleField::TERRAIN_CONTRAST,
    ProfileWireFormat::SHORT_PERCENT,
    65,
    nullptr,
    0, 100, 5,
  },
  {
    PageSettingId::TERRAIN_BRIGHTNESS,
    PageSettingType::INTEGER,
    N_("Terrain brightness"),
    N_("Defines the brightness (whiteness) of the terrain rendering. "
       "This controls the average illumination of the terrain."),
    "OverrideTerrainBrightness",
    ProfileKeys::TerrainBrightness,
    TerrainBundleField::TERRAIN_BRIGHTNESS,
    ProfileWireFormat::SHORT_PERCENT,
    192,
    nullptr,
    0, 100, 5,
  },
  {
    PageSettingId::TERRAIN_CONTOURS,
    PageSettingType::ENUM,
    N_("Contours"),
    N_("Draw contour lines on the terrain. Contour mode "
       "controls density of contour lines."),
    "OverrideTerrainContours",
    ProfileKeys::TerrainContours,
    TerrainBundleField::TERRAIN_CONTOURS,
    ProfileWireFormat::UINT8_CONTOURS,
    int(Contours::OFF),
    terrain_contours_choices,
    0, 0, 0,
  },
};

static_assert(ARRAY_SIZE(catalog) == unsigned(PageSettingId::COUNT),
              "Catalog size must match PageSettingId::COUNT");

[[nodiscard]]
int
GetBundleField(const Bundle &bundle, TerrainBundleField field) noexcept
{
  switch (field) {
  case TerrainBundleField::TERRAIN_ENABLE:
    return bundle.terrain.enable ? 1 : 0;
  case TerrainBundleField::TOPOGRAPHY_ENABLE:
    return bundle.topography_enabled ? 1 : 0;
  case TerrainBundleField::TERRAIN_RAMP:
    return int(bundle.terrain.ramp);
  case TerrainBundleField::TERRAIN_SLOPE_SHADING:
    return int(bundle.terrain.slope_shading);
  case TerrainBundleField::TERRAIN_CONTRAST:
    return TerrainByteToPercent(bundle.terrain.contrast);
  case TerrainBundleField::TERRAIN_BRIGHTNESS:
    return TerrainByteToPercent(bundle.terrain.brightness);
  case TerrainBundleField::TERRAIN_CONTOURS:
    return int(bundle.terrain.contours);
  }

  gcc_unreachable();
}

void
SetBundleField(Bundle &bundle, TerrainBundleField field, int value) noexcept
{
  switch (field) {
  case TerrainBundleField::TERRAIN_ENABLE:
    bundle.terrain.enable = value != 0;
    return;
  case TerrainBundleField::TOPOGRAPHY_ENABLE:
    bundle.topography_enabled = value != 0;
    return;
  case TerrainBundleField::TERRAIN_RAMP:
    bundle.terrain.ramp = unsigned(value);
    return;
  case TerrainBundleField::TERRAIN_SLOPE_SHADING:
    bundle.terrain.slope_shading = SlopeShading(value);
    return;
  case TerrainBundleField::TERRAIN_CONTRAST:
    bundle.terrain.contrast = TerrainPercentToByte(short(value));
    return;
  case TerrainBundleField::TERRAIN_BRIGHTNESS:
    bundle.terrain.brightness = TerrainPercentToByte(short(value));
    return;
  case TerrainBundleField::TERRAIN_CONTOURS:
    bundle.terrain.contours = Contours(value);
    return;
  }

  gcc_unreachable();
}

[[nodiscard]]
int
LoadProfileValue(const PageSettingDescriptor &desc) noexcept
{
  switch (desc.profile_wire) {
  case ProfileWireFormat::BOOL: {
    bool value = desc.profile_default != 0;
    Profile::Get(desc.profile_key, value);
    return value ? 1 : 0;
  }
  case ProfileWireFormat::UNSIGNED: {
    unsigned value = unsigned(desc.profile_default);
    if (!Profile::Get(desc.profile_key, value) ||
        value >= TerrainRendererSettings::NUM_RAMPS)
      value = unsigned(desc.profile_default);
    return int(value);
  }
  case ProfileWireFormat::UINT8_SLOPE: {
    uint8_t value = uint8_t(desc.profile_default);
    if (!Profile::Get(desc.profile_key, value) ||
        value >= uint8_t(SlopeShading::COUNT))
      value = uint8_t(desc.profile_default);
    return int(value);
  }
  case ProfileWireFormat::UINT8_CONTOURS: {
    uint8_t value = uint8_t(desc.profile_default);
    if (!Profile::Get(desc.profile_key, value) ||
        value >= uint8_t(Contours::COUNT))
      value = uint8_t(desc.profile_default);
    return int(value);
  }
  case ProfileWireFormat::SHORT_PERCENT: {
    short value = short(desc.profile_default);
    Profile::Get(desc.profile_key, value);
    return TerrainByteToPercent(value);
  }
  }

  gcc_unreachable();
}

void
SaveProfileValue(const PageSettingDescriptor &desc, int value) noexcept
{
  switch (desc.profile_wire) {
  case ProfileWireFormat::BOOL:
    Profile::Set(desc.profile_key, value != 0);
    return;
  case ProfileWireFormat::UNSIGNED:
    Profile::Set(desc.profile_key, unsigned(value));
    return;
  case ProfileWireFormat::UINT8_SLOPE:
    Profile::SetEnum(desc.profile_key, SlopeShading(value));
    return;
  case ProfileWireFormat::UINT8_CONTOURS:
    Profile::SetEnum(desc.profile_key, Contours(value));
    return;
  case ProfileWireFormat::SHORT_PERCENT:
    Profile::Set(desc.profile_key, TerrainPercentToByte(short(value)));
    return;
  }

  gcc_unreachable();
}

} // namespace

unsigned
Count() noexcept
{
  return unsigned(PageSettingId::COUNT);
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  assert(unsigned(id) < unsigned(PageSettingId::COUNT));
  return catalog[unsigned(id)];
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  assert(index < unsigned(PageSettingId::COUNT));
  return catalog[index];
}

bool
IsValidValue(PageSettingId id, int value) noexcept
{
  if (value == PageSettingOverrides::INHERIT)
    return true;

  const auto &desc = Get(id);

  if (desc.type == PageSettingType::INTEGER) {
    if (value < desc.int_min || value > desc.int_max)
      return false;
    if (desc.int_step <= 0)
      return true;
    return (value - desc.int_min) % desc.int_step == 0;
  }

  assert(desc.choices != nullptr);
  for (const StaticEnumChoice *c = desc.choices;
       c->display_string != nullptr; ++c)
    if (int(c->id) == value)
      return true;
  return false;
}

int
GetLive(PageSettingId id) noexcept
{
  Bundle bundle;
  ReadLive(bundle);
  return GetValue(bundle, id);
}

void
SetLive(PageSettingId id, int value) noexcept
{
  if (!IsValidValue(id, value))
    return;

  Bundle bundle;
  ReadLive(bundle);
  SetValue(bundle, id, value);
  ApplyLive(bundle);
}

int
LoadGlobal(PageSettingId id) noexcept
{
  return LoadProfileValue(Get(id));
}

void
SaveGlobal(PageSettingId id, int value) noexcept
{
  if (!IsValidValue(id, value))
    return;

  SaveProfileValue(Get(id), value);
}

int
GetValue(const Bundle &bundle, PageSettingId id) noexcept
{
  return GetBundleField(bundle, Get(id).bundle_field);
}

void
SetValue(Bundle &bundle, PageSettingId id, int value) noexcept
{
  if (!IsValidValue(id, value))
    return;

  SetBundleField(bundle, Get(id).bundle_field, value);
}

void
ReadLive(Bundle &bundle) noexcept
{
  const MapSettings &settings = CommonInterface::GetMapSettings();
  bundle.terrain = settings.terrain;
  bundle.topography_enabled = settings.topography_enabled;
}

void
ApplyLive(const Bundle &bundle) noexcept
{
  MapSettings &settings = CommonInterface::SetMapSettings();
  settings.terrain = bundle.terrain;
  settings.topography_enabled = bundle.topography_enabled;
}

void
LoadGlobal(Bundle &bundle) noexcept
{
  bundle.terrain.SetDefaults();
  Profile::LoadTerrainRendererSettings(Profile::map, bundle.terrain);

  bool topography = true;
  Profile::Get(ProfileKeys::DrawTopography, topography);
  bundle.topography_enabled = topography;
}

bool
SaveGlobal(const Bundle &current, const Bundle &initial) noexcept
{
  bool changed = false;

  if (current.terrain != initial.terrain) {
    Profile::Set(ProfileKeys::DrawTerrain, current.terrain.enable);
    Profile::Set(ProfileKeys::TerrainContrast, current.terrain.contrast);
    Profile::Set(ProfileKeys::TerrainBrightness, current.terrain.brightness);
    Profile::Set(ProfileKeys::TerrainRamp, current.terrain.ramp);
    Profile::SetEnum(ProfileKeys::SlopeShadingType,
                     current.terrain.slope_shading);
    Profile::SetEnum(ProfileKeys::TerrainContours, current.terrain.contours);
    changed = true;
  }

  if (current.topography_enabled != initial.topography_enabled) {
    Profile::Set(ProfileKeys::DrawTopography, current.topography_enabled);
    changed = true;
  }

  return changed;
}

} // namespace TerrainDisplaySetting
