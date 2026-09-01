// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TerrainDisplaySetting.hpp"

#include "Interface.hpp"
#include "MapSettings.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingProfile.hpp"
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
  PageSettingCatalog::TerrainBool(
    PageSettingId::TERRAIN_ENABLE,
    N_("Terrain Display"),
    N_("Draw a digital elevation terrain on the map."),
    "OverrideTerrainEnable",
    ProfileKeys::DrawTerrain,
    TerrainBundleField::TERRAIN_ENABLE),
  PageSettingCatalog::TerrainBool(
    PageSettingId::TOPOGRAPHY_ENABLE,
    N_("Topography display"),
    N_("Draw topographical features (roads, rivers, lakes etc.) on the map."),
    "OverrideTopographyEnable",
    ProfileKeys::DrawTopography,
    TerrainBundleField::TOPOGRAPHY_ENABLE),
  PageSettingCatalog::TerrainEnum(
    PageSettingId::TERRAIN_COLORS,
    N_("Terrain colors"),
    N_("Defines the color ramp used in terrain rendering."),
    "OverrideTerrainColors",
    ProfileKeys::TerrainRamp,
    TerrainBundleField::TERRAIN_RAMP,
    ProfileWireFormat::UNSIGNED, 0, terrain_ramp_choices),
  PageSettingCatalog::TerrainEnum(
    PageSettingId::TERRAIN_SLOPE_SHADING,
    N_("Slope shading"),
    N_("The terrain can be shaded among slopes to indicate either "
       "wind direction, sun position, a geographically fixed shading from "
       "North-West, or a screen-relative fixed shading from top left."),
    "OverrideTerrainSlopeShading",
    ProfileKeys::SlopeShadingType,
    TerrainBundleField::TERRAIN_SLOPE_SHADING,
    ProfileWireFormat::UINT8_SLOPE, int(SlopeShading::FIXED),
    terrain_slope_shading_choices),
  PageSettingCatalog::TerrainInteger(
    PageSettingId::TERRAIN_CONTRAST,
    N_("Terrain contrast"),
    N_("Defines the amount of Phong shading in the terrain rendering. "
       "Use large values to emphasise terrain slope, smaller values if "
       "flying in steep mountains."),
    "OverrideTerrainContrast",
    ProfileKeys::TerrainContrast,
    TerrainBundleField::TERRAIN_CONTRAST, 65, 0, 100, 5),
  PageSettingCatalog::TerrainInteger(
    PageSettingId::TERRAIN_BRIGHTNESS,
    N_("Terrain brightness"),
    N_("Defines the brightness (whiteness) of the terrain rendering. "
       "This controls the average illumination of the terrain."),
    "OverrideTerrainBrightness",
    ProfileKeys::TerrainBrightness,
    TerrainBundleField::TERRAIN_BRIGHTNESS, 192, 0, 100, 5),
  PageSettingCatalog::TerrainEnum(
    PageSettingId::TERRAIN_CONTOURS,
    N_("Contours"),
    N_("Draw contour lines on the terrain. Contour mode "
       "controls density of contour lines."),
    "OverrideTerrainContours",
    ProfileKeys::TerrainContours,
    TerrainBundleField::TERRAIN_CONTOURS,
    ProfileWireFormat::UINT8_CONTOURS, int(Contours::OFF),
    terrain_contours_choices),
};

static_assert(ARRAY_SIZE(catalog) == PageSettingTerrainCount,
              "Catalog size must match terrain PageSettingId count");

struct FieldAccessor {
  int (*get)(const Bundle &) noexcept;
  void (*set)(Bundle &, int) noexcept;
};

[[nodiscard]]
int
GetTerrainEnable(const Bundle &bundle) noexcept
{
  return bundle.terrain.enable ? 1 : 0;
}

void
SetTerrainEnable(Bundle &bundle, int value) noexcept
{
  bundle.terrain.enable = value != 0;
}

[[nodiscard]]
int
GetTopographyEnable(const Bundle &bundle) noexcept
{
  return bundle.topography_enabled ? 1 : 0;
}

void
SetTopographyEnable(Bundle &bundle, int value) noexcept
{
  bundle.topography_enabled = value != 0;
}

[[nodiscard]]
int
GetTerrainRamp(const Bundle &bundle) noexcept
{
  return int(bundle.terrain.ramp);
}

void
SetTerrainRamp(Bundle &bundle, int value) noexcept
{
  bundle.terrain.ramp = unsigned(value);
}

[[nodiscard]]
int
GetTerrainSlopeShading(const Bundle &bundle) noexcept
{
  return int(bundle.terrain.slope_shading);
}

void
SetTerrainSlopeShading(Bundle &bundle, int value) noexcept
{
  bundle.terrain.slope_shading = SlopeShading(value);
}

[[nodiscard]]
int
GetTerrainContrast(const Bundle &bundle) noexcept
{
  return TerrainByteToPercent(bundle.terrain.contrast);
}

void
SetTerrainContrast(Bundle &bundle, int value) noexcept
{
  bundle.terrain.contrast = TerrainPercentToByte(short(value));
}

[[nodiscard]]
int
GetTerrainBrightness(const Bundle &bundle) noexcept
{
  return TerrainByteToPercent(bundle.terrain.brightness);
}

void
SetTerrainBrightness(Bundle &bundle, int value) noexcept
{
  bundle.terrain.brightness = TerrainPercentToByte(short(value));
}

[[nodiscard]]
int
GetTerrainContours(const Bundle &bundle) noexcept
{
  return int(bundle.terrain.contours);
}

void
SetTerrainContours(Bundle &bundle, int value) noexcept
{
  bundle.terrain.contours = Contours(value);
}

static constexpr FieldAccessor field_accessors[] = {
  { GetTerrainEnable, SetTerrainEnable },
  { GetTopographyEnable, SetTopographyEnable },
  { GetTerrainRamp, SetTerrainRamp },
  { GetTerrainSlopeShading, SetTerrainSlopeShading },
  { GetTerrainContrast, SetTerrainContrast },
  { GetTerrainBrightness, SetTerrainBrightness },
  { GetTerrainContours, SetTerrainContours },
};

static_assert(ARRAY_SIZE(field_accessors) ==
              unsigned(TerrainBundleField::COUNT),
              "Terrain field accessors must match TerrainBundleField::COUNT");

[[nodiscard]]
int
GetBundleField(const Bundle &bundle, TerrainBundleField field) noexcept
{
  return field_accessors[unsigned(field)].get(bundle);
}

void
SetBundleField(Bundle &bundle, TerrainBundleField field, int value) noexcept
{
  field_accessors[unsigned(field)].set(bundle, value);
}

} // namespace

unsigned
Count() noexcept
{
  return PageSettingTerrainCount;
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  assert(unsigned(id) < PageSettingTerrainCount);
  return catalog[unsigned(id)];
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  assert(index < PageSettingTerrainCount);
  return catalog[index];
}

bool
IsValidValue(PageSettingId id, int value) noexcept
{
  return PageSettingCatalog::IsValidValue(Get(id), value);
}

int
GetLive(PageSettingId id) noexcept
{
  return PageSettingCatalog::GetLive<Bundle>(id, ReadLive, GetValue);
}

void
SetLive(PageSettingId id, int value) noexcept
{
  PageSettingCatalog::SetLive<Bundle>(id, value, ReadLive, ApplyLive,
                                      SetValue, IsValidValue);
}

int
LoadGlobal(PageSettingId id) noexcept
{
  return PageSettingProfile::Load(Get(id));
}

void
SaveGlobal(PageSettingId id, int value) noexcept
{
  if (!IsValidValue(id, value))
    return;

  PageSettingProfile::Save(Get(id), value);
}

int
GetValue(const Bundle &bundle, PageSettingId id) noexcept
{
  return GetBundleField(bundle, Get(id).bundle_field.terrain);
}

void
SetValue(Bundle &bundle, PageSettingId id, int value) noexcept
{
  if (!IsValidValue(id, value))
    return;

  SetBundleField(bundle, Get(id).bundle_field.terrain, value);
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
