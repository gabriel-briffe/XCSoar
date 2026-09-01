// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Elements/ElementsDisplaySetting.hpp"

#include "Elements/ElementsDisplayChoices.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingFieldAccessors.hpp"
#include "PageSettingModuleImpl.hpp"
#include "PageSettingProfile.hpp"
#include "Profile/Current.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "util/Macros.hpp"

#include <cassert>
#include <cstdint>

namespace ElementsDisplaySetting {

void ReadLive(Bundle &) noexcept;
void ApplyLive(const Bundle &) noexcept;

namespace {

using Bundle = ElementsDisplaySetting::Bundle;

static constexpr PageSettingDescriptor catalog[] = {
  PageSettingCatalog::CatalogEnum(
    PageSettingId::GROUND_TRACK,
    N_("Ground track"),
    N_("Display the ground track as a grey line on the map."),
    "OverrideGroundTrack",
    ProfileKeys::DisplayTrackBearing,
    {.elements = ElementsBundleField::GROUND_TRACK},
    ProfileWireFormat::UINT8_ENUM, int(DisplayGroundTrack::AUTO),
    ground_track_mode_choices),
  PageSettingCatalog::CatalogBool(
    PageSettingId::FLARM_TRAFFIC,
    N_("FLARM Traffic"),
    N_("This enables the display of FLARM traffic on the map window."),
    "OverrideFlarmTraffic",
    ProfileKeys::EnableFLARMMap,
    {.elements = ElementsBundleField::FLARM_TRAFFIC}),
  PageSettingCatalog::CatalogBool(
    PageSettingId::FADE_TRAFFIC,
    N_("Fade traffic"),
    N_("Keep showing traffic for a while after it has disappeared."),
    "OverrideFadeTraffic",
    ProfileKeys::FadeTraffic,
    {.elements = ElementsBundleField::FADE_TRAFFIC}),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::TRAIL_LENGTH,
    N_("Trail length"),
    N_("Determines whether and how long a snail trail is drawn behind the "
       "glider."),
    "OverrideTrailLength",
    ProfileKeys::SnailTrail,
    {.elements = ElementsBundleField::TRAIL_LENGTH},
    ProfileWireFormat::UINT8_ENUM, int(TrailSettings::Length::LONG),
    trail_length_choices),
  PageSettingCatalog::CatalogBool(
    PageSettingId::TRAIL_DRIFT,
    N_("Trail drift"),
    N_("Determines whether the snail trail is drifted with the wind when "
       "displayed in circling mode at near map scales. Switched Off, the "
       "snail trail stays uncompensated for wind drift."),
    "OverrideTrailDrift",
    ProfileKeys::TrailDrift,
    {.elements = ElementsBundleField::TRAIL_DRIFT}),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::TRAIL_TYPE,
    N_("Trail type"),
    N_("Sets the type of the snail trail display."),
    "OverrideTrailType",
    ProfileKeys::SnailType,
    {.elements = ElementsBundleField::TRAIL_TYPE},
    ProfileWireFormat::UINT8_ENUM, int(TrailSettings::Type::VARIO_1),
    trail_type_choices),
  PageSettingCatalog::CatalogBool(
    PageSettingId::TRAIL_SCALED,
    N_("Trail scaled"),
    N_("If set to ON the snail trail width is scaled according to the vario "
       "signal."),
    "OverrideTrailScaled",
    ProfileKeys::SnailWidthScale,
    {.elements = ElementsBundleField::TRAIL_SCALED}),
  PageSettingCatalog::CatalogBool(
    PageSettingId::DETOUR_COST_MARKERS,
    N_("Detour cost markers"),
    N_("If the aircraft heading deviates from the current waypoint, markers "
       "are displayed at points ahead of the aircraft. The value of each "
       "marker is the extra distance required to reach that point as a "
       "percentage of straight-line distance to the waypoint."),
    "OverrideDetourCostMarkers",
    ProfileKeys::DetourCostMarker,
    {.elements = ElementsBundleField::DETOUR_COST_MARKERS}, 0),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::AIRCRAFT_SYMBOL,
    N_("Aircraft symbol"),
    N_("Select the aircraft icon drawn on the map."),
    "OverrideAircraftSymbol",
    ProfileKeys::AircraftSymbol,
    {.elements = ElementsBundleField::AIRCRAFT_SYMBOL},
    ProfileWireFormat::UINT8_ENUM, int(AircraftSymbol::SIMPLE),
    aircraft_symbol_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::WIND_ARROW_STYLE,
    N_("Wind arrow"),
    N_("Determines the way the wind arrow is drawn on the map."),
    "OverrideWindArrowStyle",
    ProfileKeys::WindArrowStyle,
    {.elements = ElementsBundleField::WIND_ARROW_STYLE},
    ProfileWireFormat::UINT8_ENUM, int(WindArrowStyle::ARROW_HEAD),
    wind_arrow_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::ONLINE_TRAFFIC_MAP_MODE,
    NC_("Setting", "Online traffic on map"),
    N_("Show traffic from SkyLines and XCSoar Cloud on the map."),
    "OverrideOnlineTrafficMapMode",
    ProfileKeys::OnlineTrafficMapMode,
    {.elements = ElementsBundleField::ONLINE_TRAFFIC_MAP_MODE},
    ProfileWireFormat::UINT8_ENUM,
    int(DisplayOnlineTrafficMapMode::SYMBOL),
    online_traffic_map_mode_choices),
  PageSettingCatalog::CatalogBool(
    PageSettingId::DISTANCE_RINGS,
    NC_("Setting", "Distance rings"),
    N_("Display distance rings around the aircraft on the map."),
    "OverrideDistanceRings",
    ProfileKeys::DistanceRingsEnabled,
    {.elements = ElementsBundleField::DISTANCE_RINGS}, 0),
};

static_assert(ARRAY_SIZE(catalog) == PageSettingElementsCount,
              "Catalog size must match elements PageSettingId count");

struct FieldAccessor {
  int (*get)(const Bundle &) noexcept;
  void (*set)(Bundle &, int) noexcept;
};

PAGE_SETTING_FIELD_ENUM(GroundTrack, display_ground_track, DisplayGroundTrack)
PAGE_SETTING_FIELD_BOOL(FlarmTraffic, show_flarm_on_map)
PAGE_SETTING_FIELD_BOOL(FadeTraffic, fade_traffic)
PAGE_SETTING_FIELD_ENUM(TrailLength, trail.length, TrailSettings::Length)
PAGE_SETTING_FIELD_BOOL(TrailDrift, trail.wind_drift_enabled)
PAGE_SETTING_FIELD_ENUM(TrailType, trail.type, TrailSettings::Type)
PAGE_SETTING_FIELD_BOOL(TrailScaled, trail.scaling_enabled)
PAGE_SETTING_FIELD_BOOL(DetourCostMarkers, detour_cost_markers_enabled)
PAGE_SETTING_FIELD_ENUM(AircraftSymbol, aircraft_symbol, AircraftSymbol)
PAGE_SETTING_FIELD_ENUM(WindArrowStyle, wind_arrow_style, WindArrowStyle)
PAGE_SETTING_FIELD_ENUM(OnlineTrafficMapMode, online_traffic_map_mode,
                          DisplayOnlineTrafficMapMode)
PAGE_SETTING_FIELD_BOOL(DistanceRings, distance_rings_enabled)

static constexpr FieldAccessor field_accessors[] = {
  PAGE_SETTING_FIELD_ROW(GroundTrack),
  PAGE_SETTING_FIELD_ROW(FlarmTraffic),
  PAGE_SETTING_FIELD_ROW(FadeTraffic),
  PAGE_SETTING_FIELD_ROW(TrailLength),
  PAGE_SETTING_FIELD_ROW(TrailDrift),
  PAGE_SETTING_FIELD_ROW(TrailType),
  PAGE_SETTING_FIELD_ROW(TrailScaled),
  PAGE_SETTING_FIELD_ROW(DetourCostMarkers),
  PAGE_SETTING_FIELD_ROW(AircraftSymbol),
  PAGE_SETTING_FIELD_ROW(WindArrowStyle),
  PAGE_SETTING_FIELD_ROW(OnlineTrafficMapMode),
  PAGE_SETTING_FIELD_ROW(DistanceRings),
};

static_assert(ARRAY_SIZE(field_accessors) == unsigned(ElementsBundleField::COUNT),
              "Elements field accessors must match ElementsBundleField::COUNT");

[[nodiscard]]
ElementsBundleField
FieldFromDescriptor(const PageSettingDescriptor &desc) noexcept
{
  return desc.bundle_field.elements;
}

[[nodiscard]]
int
GetBundleField(const Bundle &bundle, ElementsBundleField field) noexcept
{
  return field_accessors[unsigned(field)].get(bundle);
}

void
SetBundleField(Bundle &bundle, ElementsBundleField field, int value) noexcept
{
  field_accessors[unsigned(field)].set(bundle, value);
}

[[nodiscard]]
int
LoadOnlineTrafficMapMode() noexcept
{
  const auto &desc = catalog[unsigned(ElementsBundleField::
                                      ONLINE_TRAFFIC_MAP_MODE)];
  unsigned value = unsigned(desc.profile_default);
  if ((!Profile::Get(desc.profile_key, value) ||
       !PageSettingCatalog::IsValidValue(desc, int(value))) &&
      (!Profile::Get(ProfileKeys::SkyLinesTrafficMapMode, value) ||
       !PageSettingCatalog::IsValidValue(desc, int(value))))
    value = unsigned(desc.profile_default);
  return int(value);
}

using Impl = PageSettingModuleImpl::Module<
  Bundle, ElementsBundleField, catalog, PageSettingElementsCount,
  PageSettingElementsStart, FieldFromDescriptor,
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
  if (id == PageSettingId::ONLINE_TRAFFIC_MAP_MODE)
    return LoadOnlineTrafficMapMode();

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
  bundle.display_ground_track = settings.display_ground_track;
  bundle.show_flarm_on_map = settings.show_flarm_on_map;
  bundle.fade_traffic = settings.fade_traffic;
  bundle.trail = settings.trail;
  bundle.detour_cost_markers_enabled = settings.detour_cost_markers_enabled;
  bundle.aircraft_symbol = settings.aircraft_symbol;
  bundle.wind_arrow_style = settings.wind_arrow_style;
  bundle.online_traffic_map_mode = settings.online_traffic_map_mode;
  bundle.distance_rings_enabled = settings.distance_rings_enabled;
}

void
ApplyLive(const Bundle &bundle) noexcept
{
  MapSettings &settings = CommonInterface::SetMapSettings();
  settings.display_ground_track = bundle.display_ground_track;
  settings.show_flarm_on_map = bundle.show_flarm_on_map;
  settings.fade_traffic = bundle.fade_traffic;
  settings.trail = bundle.trail;
  settings.detour_cost_markers_enabled = bundle.detour_cost_markers_enabled;
  settings.aircraft_symbol = bundle.aircraft_symbol;
  settings.wind_arrow_style = bundle.wind_arrow_style;
  settings.online_traffic_map_mode = bundle.online_traffic_map_mode;
  settings.distance_rings_enabled = bundle.distance_rings_enabled;
}

void
LoadGlobal(Bundle &bundle) noexcept
{
  Impl::LoadGlobalBundle(bundle);
}

bool
SaveGlobal(const Bundle &current, const Bundle &initial) noexcept
{
  return Impl::SaveGlobalBundle(current, initial);
}

} // namespace ElementsDisplaySetting
