// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TerrainDisplaySetting.hpp"

#include "Interface.hpp"
#include "MapSettings.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingFieldAccessors.hpp"
#include "PageSettingModuleImpl.hpp"
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

void ReadLive(Bundle &) noexcept;
void ApplyLive(const Bundle &) noexcept;

namespace {

using Bundle = TerrainDisplaySetting::Bundle;

static constexpr PageSettingDescriptor catalog[] = {
  PageSettingCatalog::CatalogBool(
    PageSettingId::TERRAIN_ENABLE,
    N_("Terrain Display"),
    N_("Draw a digital elevation terrain on the map."),
    "OverrideTerrainEnable",
    ProfileKeys::DrawTerrain,
    {.terrain = TerrainBundleField::TERRAIN_ENABLE}),
  PageSettingCatalog::CatalogBool(
    PageSettingId::TOPOGRAPHY_ENABLE,
    N_("Topography display"),
    N_("Draw topographical features (roads, rivers, lakes etc.) on the map."),
    "OverrideTopographyEnable",
    ProfileKeys::DrawTopography,
    {.terrain = TerrainBundleField::TOPOGRAPHY_ENABLE}),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::TERRAIN_COLORS,
    N_("Terrain colors"),
    N_("Defines the color ramp used in terrain rendering."),
    "OverrideTerrainColors",
    ProfileKeys::TerrainRamp,
    {.terrain = TerrainBundleField::TERRAIN_RAMP},
    ProfileWireFormat::UNSIGNED, 0, terrain_ramp_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::TERRAIN_SLOPE_SHADING,
    N_("Slope shading"),
    N_("The terrain can be shaded among slopes to indicate either "
       "wind direction, sun position, a geographically fixed shading from "
       "North-West, or a screen-relative fixed shading from top left."),
    "OverrideTerrainSlopeShading",
    ProfileKeys::SlopeShadingType,
    {.terrain = TerrainBundleField::TERRAIN_SLOPE_SHADING},
    ProfileWireFormat::UINT8_ENUM, int(SlopeShading::FIXED),
    terrain_slope_shading_choices),
  PageSettingCatalog::CatalogInteger(
    PageSettingId::TERRAIN_CONTRAST,
    N_("Terrain contrast"),
    N_("Defines the amount of Phong shading in the terrain rendering. "
       "Use large values to emphasise terrain slope, smaller values if "
       "flying in steep mountains."),
    "OverrideTerrainContrast",
    ProfileKeys::TerrainContrast,
    {.terrain = TerrainBundleField::TERRAIN_CONTRAST},
    ProfileWireFormat::SHORT_PERCENT, 65, 0, 100, 5),
  PageSettingCatalog::CatalogInteger(
    PageSettingId::TERRAIN_BRIGHTNESS,
    N_("Terrain brightness"),
    N_("Defines the brightness (whiteness) of the terrain rendering. "
       "This controls the average illumination of the terrain."),
    "OverrideTerrainBrightness",
    ProfileKeys::TerrainBrightness,
    {.terrain = TerrainBundleField::TERRAIN_BRIGHTNESS},
    ProfileWireFormat::SHORT_PERCENT, 192, 0, 100, 5),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::TERRAIN_CONTOURS,
    N_("Contours"),
    N_("Draw contour lines on the terrain. Contour mode "
       "controls density of contour lines."),
    "OverrideTerrainContours",
    ProfileKeys::TerrainContours,
    {.terrain = TerrainBundleField::TERRAIN_CONTOURS},
    ProfileWireFormat::UINT8_ENUM, int(Contours::OFF),
    terrain_contours_choices),
};

static_assert(ARRAY_SIZE(catalog) == PageSettingTerrainCount,
              "Catalog size must match terrain PageSettingId count");

struct FieldAccessor {
  int (*get)(const Bundle &) noexcept;
  void (*set)(Bundle &, int) noexcept;
};

PAGE_SETTING_FIELD_BOOL(TerrainEnable, terrain.enable)
PAGE_SETTING_FIELD_BOOL(TopographyEnable, topography_enabled)
PAGE_SETTING_FIELD_ENUM(TerrainRamp, terrain.ramp, unsigned)
PAGE_SETTING_FIELD_ENUM(TerrainSlopeShading, terrain.slope_shading,
                          SlopeShading)

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

PAGE_SETTING_FIELD_ENUM(TerrainContours, terrain.contours, Contours)

static constexpr FieldAccessor field_accessors[] = {
  PAGE_SETTING_FIELD_ROW(TerrainEnable),
  PAGE_SETTING_FIELD_ROW(TopographyEnable),
  PAGE_SETTING_FIELD_ROW(TerrainRamp),
  PAGE_SETTING_FIELD_ROW(TerrainSlopeShading),
  { GetTerrainContrast, SetTerrainContrast },
  { GetTerrainBrightness, SetTerrainBrightness },
  PAGE_SETTING_FIELD_ROW(TerrainContours),
};

static_assert(ARRAY_SIZE(field_accessors) == unsigned(TerrainBundleField::COUNT),
              "Terrain field accessors must match TerrainBundleField::COUNT");

[[nodiscard]]
TerrainBundleField
FieldFromDescriptor(const PageSettingDescriptor &desc) noexcept
{
  return desc.bundle_field.terrain;
}

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

using Impl = PageSettingModuleImpl::Module<
  Bundle, TerrainBundleField, catalog, PageSettingTerrainCount, 0,
  FieldFromDescriptor, GetBundleField, SetBundleField, ReadLive, ApplyLive>;

} // namespace

PAGE_SETTING_MODULE_FORWARD_API(Impl)

int
LoadGlobal(PageSettingId id) noexcept
{
  return Impl::LoadGlobal(id);
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
  return Impl::SaveGlobalBundle(current, initial);
}

} // namespace TerrainDisplaySetting
