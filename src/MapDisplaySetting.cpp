// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "MapDisplaySetting.hpp"

#include "Interface.hpp"
#include "MapDisplayChoices.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingProfile.hpp"
#include "Profile/Current.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "util/Macros.hpp"

#include <cassert>
#include <cstdint>

namespace MapDisplaySetting {

namespace {

static constexpr PageSettingDescriptor catalog[] = {
  PageSettingCatalog::MapEnum(
    PageSettingId::CRUISE_ORIENTATION,
    N_("Cruise orientation"),
    N_("Determines how the screen is rotated with the glider"),
    "OverrideCruiseOrientation",
    ProfileKeys::OrientationCruise,
    MapBundleField::CRUISE_ORIENTATION,
    ProfileWireFormat::UINT8_MAP_ORIENTATION, int(MapOrientation::NORTH_UP),
    map_orientation_choices),
  PageSettingCatalog::MapEnum(
    PageSettingId::CIRCLING_ORIENTATION,
    N_("Circling orientation"),
    N_("Determines how the screen is rotated with the glider while circling"),
    "OverrideCirclingOrientation",
    ProfileKeys::OrientationCircling,
    MapBundleField::CIRCLING_ORIENTATION,
    ProfileWireFormat::UINT8_MAP_ORIENTATION, int(MapOrientation::NORTH_UP),
    map_orientation_choices),
  PageSettingCatalog::MapBool(
    PageSettingId::CIRCLING_ZOOM,
    N_("Circling zoom"),
    N_("If enabled, then the map will zoom in automatically when entering "
       "circling mode and zoom out automatically when leaving circling mode."),
    "OverrideCirclingZoom",
    ProfileKeys::CircleZoom,
    MapBundleField::CIRCLING_ZOOM),
  PageSettingCatalog::MapEnum(
    PageSettingId::MAP_SHIFT_BIAS,
    N_("Map shift reference"),
    N_("Determines what is used to shift the glider from the map center"),
    "OverrideMapShiftBias",
    ProfileKeys::MapShiftBias,
    MapBundleField::MAP_SHIFT_BIAS,
    ProfileWireFormat::UINT8_MAP_SHIFT_BIAS, int(MapShiftBias::NONE),
    map_shift_bias_choices),
  PageSettingCatalog::MapInteger(
    PageSettingId::GLIDER_SCREEN_POSITION,
    N_("Glider position offset"),
    N_("Defines the location of the glider drawn on the screen in percent "
       "from the screen edge."),
    "OverrideGliderScreenPosition",
    ProfileKeys::GliderScreenPosition,
    MapBundleField::GLIDER_SCREEN_POSITION, 20, 10, 50, 5),
};

static_assert(ARRAY_SIZE(catalog) == PageSettingMapCount,
              "Catalog size must match map PageSettingId count");

struct FieldAccessor {
  int (*get)(const Bundle &) noexcept;
  void (*set)(Bundle &, int) noexcept;
};

[[nodiscard]]
int
GetCruiseOrientation(const Bundle &bundle) noexcept
{
  return int(bundle.cruise_orientation);
}

void
SetCruiseOrientation(Bundle &bundle, int value) noexcept
{
  bundle.cruise_orientation = MapOrientation(value);
}

[[nodiscard]]
int
GetCirclingOrientation(const Bundle &bundle) noexcept
{
  return int(bundle.circling_orientation);
}

void
SetCirclingOrientation(Bundle &bundle, int value) noexcept
{
  bundle.circling_orientation = MapOrientation(value);
}

[[nodiscard]]
int
GetCirclingZoom(const Bundle &bundle) noexcept
{
  return bundle.circle_zoom_enabled ? 1 : 0;
}

void
SetCirclingZoom(Bundle &bundle, int value) noexcept
{
  bundle.circle_zoom_enabled = value != 0;
}

[[nodiscard]]
int
GetMapShiftBias(const Bundle &bundle) noexcept
{
  return int(bundle.map_shift_bias);
}

void
SetMapShiftBias(Bundle &bundle, int value) noexcept
{
  bundle.map_shift_bias = MapShiftBias(value);
}

[[nodiscard]]
int
GetGliderScreenPosition(const Bundle &bundle) noexcept
{
  return bundle.glider_screen_position;
}

void
SetGliderScreenPosition(Bundle &bundle, int value) noexcept
{
  bundle.glider_screen_position = value;
}

static constexpr FieldAccessor field_accessors[] = {
  { GetCruiseOrientation, SetCruiseOrientation },
  { GetCirclingOrientation, SetCirclingOrientation },
  { GetCirclingZoom, SetCirclingZoom },
  { GetMapShiftBias, SetMapShiftBias },
  { GetGliderScreenPosition, SetGliderScreenPosition },
};

static_assert(ARRAY_SIZE(field_accessors) == unsigned(MapBundleField::COUNT),
              "Map field accessors must match MapBundleField::COUNT");

[[nodiscard]]
int
GetBundleField(const Bundle &bundle, MapBundleField field) noexcept
{
  return field_accessors[unsigned(field)].get(bundle);
}

void
SetBundleField(Bundle &bundle, MapBundleField field, int value) noexcept
{
  field_accessors[unsigned(field)].set(bundle, value);
}

} // namespace

unsigned
Count() noexcept
{
  return PageSettingMapCount;
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  assert(unsigned(id) >= PageSettingTerrainCount);
  assert(unsigned(id) < unsigned(PageSettingId::COUNT));
  return catalog[unsigned(id) - PageSettingTerrainCount];
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  assert(index < PageSettingMapCount);
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
  return GetBundleField(bundle, Get(id).bundle_field.map);
}

void
SetValue(Bundle &bundle, PageSettingId id, int value) noexcept
{
  if (!IsValidValue(id, value))
    return;

  SetBundleField(bundle, Get(id).bundle_field.map, value);
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

  for (unsigned i = 0; i < Count(); ++i) {
    const auto id = PageSettingId(unsigned(PageSettingId::CRUISE_ORIENTATION) + i);
    SetValue(bundle, id, LoadGlobal(id));
  }
}

bool
SaveGlobal(const Bundle &current, const Bundle &initial) noexcept
{
  bool changed = false;

  for (unsigned i = 0; i < Count(); ++i) {
    const auto id = PageSettingId(unsigned(PageSettingId::CRUISE_ORIENTATION) + i);
    if (GetValue(current, id) == GetValue(initial, id))
      continue;

    SaveGlobal(id, GetValue(current, id));
    changed = true;
  }

  return changed;
}

} // namespace MapDisplaySetting
