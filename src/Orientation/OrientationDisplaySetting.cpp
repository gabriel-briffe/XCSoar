// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Orientation/OrientationDisplaySetting.hpp"

#include "Interface.hpp"
#include "Orientation/OrientationDisplayChoices.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingFieldAccessors.hpp"
#include "PageSettingModuleImpl.hpp"
#include "PageSettingProfile.hpp"
#include "Profile/Keys.hpp"
#include "util/Macros.hpp"

#include <cassert>
#include <cstdint>

namespace OrientationDisplaySetting {

void ReadLive(Bundle &) noexcept;
void ApplyLive(const Bundle &) noexcept;

namespace {

using Bundle = OrientationDisplaySetting::Bundle;

static constexpr PageSettingDescriptor catalog[] = {
  PageSettingCatalog::CatalogEnum(
    PageSettingId::CRUISE_ORIENTATION,
    N_("Cruise orientation"),
    N_("Determines how the screen is rotated with the glider"),
    "OverrideCruiseOrientation",
    ProfileKeys::OrientationCruise,
    {.orientation = OrientationBundleField::CRUISE_ORIENTATION},
    ProfileWireFormat::UINT8_ENUM, int(MapOrientation::NORTH_UP),
    orientation_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::CIRCLING_ORIENTATION,
    N_("Circling orientation"),
    N_("Determines how the screen is rotated with the glider while circling"),
    "OverrideCirclingOrientation",
    ProfileKeys::OrientationCircling,
    {.orientation = OrientationBundleField::CIRCLING_ORIENTATION},
    ProfileWireFormat::UINT8_ENUM, int(MapOrientation::NORTH_UP),
    orientation_choices),
  PageSettingCatalog::CatalogBool(
    PageSettingId::CIRCLING_ZOOM,
    N_("Circling zoom"),
    N_("If enabled, then the map will zoom in automatically when entering "
       "circling mode and zoom out automatically when leaving circling mode."),
    "OverrideCirclingZoom",
    ProfileKeys::CircleZoom,
    {.orientation = OrientationBundleField::CIRCLING_ZOOM}),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::MAP_SHIFT_BIAS,
    N_("Map shift reference"),
    N_("Determines what is used to shift the glider from the map center"),
    "OverrideMapShiftBias",
    ProfileKeys::MapShiftBias,
    {.orientation = OrientationBundleField::MAP_SHIFT_BIAS},
    ProfileWireFormat::UINT8_ENUM, int(MapShiftBias::NONE),
    map_shift_bias_choices),
  PageSettingCatalog::CatalogInteger(
    PageSettingId::GLIDER_SCREEN_POSITION,
    N_("Glider position offset"),
    N_("Defines the location of the glider drawn on the screen in percent "
       "from the screen edge."),
    "OverrideGliderScreenPosition",
    ProfileKeys::GliderScreenPosition,
    {.orientation = OrientationBundleField::GLIDER_SCREEN_POSITION},
    ProfileWireFormat::INT, 20, 10, 50, 5),
};

static_assert(ARRAY_SIZE(catalog) == PageSettingOrientationCount,
              "Catalog size must match orientation PageSettingId count");

struct FieldAccessor {
  int (*get)(const Bundle &) noexcept;
  void (*set)(Bundle &, int) noexcept;
};

PAGE_SETTING_FIELD_ENUM(CruiseOrientation, cruise_orientation, MapOrientation)
PAGE_SETTING_FIELD_ENUM(CirclingOrientation, circling_orientation,
                          MapOrientation)
PAGE_SETTING_FIELD_BOOL(CirclingZoom, circle_zoom_enabled)
PAGE_SETTING_FIELD_ENUM(MapShiftBias, map_shift_bias, MapShiftBias)
PAGE_SETTING_FIELD_INT(GliderScreenPosition, glider_screen_position)

static constexpr FieldAccessor field_accessors[] = {
  PAGE_SETTING_FIELD_ROW(CruiseOrientation),
  PAGE_SETTING_FIELD_ROW(CirclingOrientation),
  PAGE_SETTING_FIELD_ROW(CirclingZoom),
  PAGE_SETTING_FIELD_ROW(MapShiftBias),
  PAGE_SETTING_FIELD_ROW(GliderScreenPosition),
};

static_assert(ARRAY_SIZE(field_accessors) ==
              unsigned(OrientationBundleField::COUNT),
              "Orientation field accessors must match "
              "OrientationBundleField::COUNT");

[[nodiscard]]
OrientationBundleField
FieldFromDescriptor(const PageSettingDescriptor &desc) noexcept
{
  return desc.bundle_field.orientation;
}

[[nodiscard]]
int
GetBundleField(const Bundle &bundle, OrientationBundleField field) noexcept
{
  return field_accessors[unsigned(field)].get(bundle);
}

void
SetBundleField(Bundle &bundle, OrientationBundleField field,
               int value) noexcept
{
  field_accessors[unsigned(field)].set(bundle, value);
}

using Impl = PageSettingModuleImpl::Module<
  Bundle, OrientationBundleField, catalog, PageSettingOrientationCount,
  PageSettingOrientationStart, FieldFromDescriptor,
  GetBundleField, SetBundleField, ReadLive, ApplyLive>;

} // namespace

unsigned
Count() noexcept
{
  return Impl::Count();
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  return Impl::Get(id);
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  return Impl::Get(index);
}

bool
IsValidValue(PageSettingId id, int value) noexcept
{
  return Impl::IsValidValue(id, value);
}

int
GetLive(PageSettingId id) noexcept
{
  return Impl::GetLive(id);
}

void
SetLive(PageSettingId id, int value) noexcept
{
  Impl::SetLive(id, value);
}

int
LoadGlobal(PageSettingId id) noexcept
{
  return Impl::LoadGlobal(id);
}

void
SaveGlobal(PageSettingId id, int value) noexcept
{
  Impl::SaveGlobal(id, value);
}

int
GetValue(const Bundle &bundle, PageSettingId id) noexcept
{
  return Impl::GetValue(bundle, id);
}

void
SetValue(Bundle &bundle, PageSettingId id, int value) noexcept
{
  Impl::SetValue(bundle, id, value);
}

void
ReadLive(Bundle &bundle) noexcept
{
  const MapSettings &settings = CommonInterface::GetMapSettings();
  bundle.cruise_orientation = settings.cruise_orientation;
  bundle.circling_orientation = settings.circling_orientation;
  bundle.circle_zoom_enabled = settings.circle_zoom_enabled;
  bundle.map_shift_bias = settings.map_shift_bias;
  bundle.glider_screen_position = settings.glider_screen_position;
}

void
ApplyLive(const Bundle &bundle) noexcept
{
  MapSettings &settings = CommonInterface::SetMapSettings();
  settings.cruise_orientation = bundle.cruise_orientation;
  settings.circling_orientation = bundle.circling_orientation;
  settings.circle_zoom_enabled = bundle.circle_zoom_enabled;
  settings.map_shift_bias = bundle.map_shift_bias;
  settings.glider_screen_position = bundle.glider_screen_position;
}

void
LoadGlobal(Bundle &bundle) noexcept
{
  MapSettings defaults;
  defaults.SetDefaults();
  bundle.cruise_orientation = defaults.cruise_orientation;
  bundle.circling_orientation = defaults.circling_orientation;
  bundle.circle_zoom_enabled = defaults.circle_zoom_enabled;
  bundle.map_shift_bias = defaults.map_shift_bias;
  bundle.glider_screen_position = defaults.glider_screen_position;
  Impl::LoadGlobalBundle(bundle);
}

bool
SaveGlobal(const Bundle &current, const Bundle &initial) noexcept
{
  return Impl::SaveGlobalBundle(current, initial);
}

} // namespace OrientationDisplaySetting
