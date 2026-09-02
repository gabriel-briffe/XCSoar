// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Waypoints/WaypointsDisplaySetting.hpp"

#include "Interface.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingFieldAccessors.hpp"
#include "PageSettingModuleImpl.hpp"
#include "PageSettingProfile.hpp"
#include "Profile/Keys.hpp"
#include "Waypoints/WaypointsDisplayChoices.hpp"
#include "util/Macros.hpp"

#include <cassert>
#include <cstdint>

namespace WaypointsDisplaySetting {

void ReadLive(Bundle &) noexcept;
void ApplyLive(const Bundle &) noexcept;

namespace {

using Bundle = WaypointsDisplaySetting::Bundle;

static constexpr PageSettingDescriptor catalog[] = {
  PageSettingCatalog::CatalogEnum(
    PageSettingId::WAYPOINT_LABEL_FORMAT,
    N_("Label format"),
    N_("Determines how labels are displayed with each waypoint"),
    "OverrideWaypointLabelFormat",
    ProfileKeys::DisplayText,
    {.waypoints = WaypointsBundleField::LABEL_FORMAT},
    ProfileWireFormat::UINT8_ENUM,
    int(WaypointRendererSettings::DisplayTextType::SHORT_NAME),
    waypoint_label_format_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::WAYPOINT_ARRIVAL_HEIGHT,
    N_("Arrival height"),
    N_("Determines how arrival height is displayed in waypoint labels"),
    "OverrideWaypointArrivalHeight",
    ProfileKeys::WaypointArrivalHeightDisplay,
    {.waypoints = WaypointsBundleField::ARRIVAL_HEIGHT},
    ProfileWireFormat::UINT8_ENUM,
    int(WaypointRendererSettings::ArrivalHeightDisplay::GLIDE),
    waypoint_arrival_height_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::WAYPOINT_LABEL_STYLE,
    N_("Label style"),
    nullptr,
    "OverrideWaypointLabelStyle",
    ProfileKeys::WaypointLabelStyle,
    {.waypoints = WaypointsBundleField::LABEL_STYLE},
    ProfileWireFormat::UINT8_ENUM,
    int(LabelShape::ROUNDED_BLACK),
    waypoint_label_style_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::WAYPOINT_LABEL_VISIBILITY,
    N_("Label visibility"),
    N_("Determines what labels are displayed."),
    "OverrideWaypointLabelVisibility",
    ProfileKeys::WaypointLabelSelection,
    {.waypoints = WaypointsBundleField::LABEL_VISIBILITY},
    ProfileWireFormat::UINT8_ENUM,
    int(WaypointRendererSettings::LabelSelection::ALL),
    waypoint_label_visibility_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::WAYPOINT_LANDABLE_SYMBOLS,
    N_("Landable symbols"),
    N_("Three styles are available: Purple circles (WinPilot style), a high "
       "contrast (monochrome) style, or orange. The rendering differs for "
       "landable field and airport. All styles mark the waypoints within "
       "reach green."),
    "OverrideWaypointLandableSymbols",
    ProfileKeys::AppIndLandable,
    {.waypoints = WaypointsBundleField::LANDABLE_SYMBOLS},
    ProfileWireFormat::UINT8_ENUM,
    int(WaypointRendererSettings::LandableStyle::PURPLE_CIRCLE),
    waypoint_landable_style_choices),
  PageSettingCatalog::CatalogInteger(
    PageSettingId::WAYPOINT_ICON_SCALE,
    N_("Waypoint icon size"),
    N_("Size of waypoint symbols on the map as a percentage of the built-in "
       "artwork (list dialogs keep a fixed row icon size)."),
    "OverrideWaypointIconScale",
    ProfileKeys::MapWaypointIconScale,
    {.waypoints = WaypointsBundleField::ICON_SCALE},
    ProfileWireFormat::INT, 100, 50, 200, 10),
  PageSettingCatalog::CatalogBool(
    PageSettingId::WAYPOINT_DETAILED_LANDABLES,
    N_("Detailed landables"),
    N_("[Off] Display fixed icons for landables.\n"
       "[On] Show landables with variable information like runway length "
       "and heading."),
    "OverrideWaypointDetailedLandables",
    ProfileKeys::AppUseSWLandablesRendering,
    {.waypoints = WaypointsBundleField::DETAILED_LANDABLES}),
  PageSettingCatalog::CatalogInteger(
    PageSettingId::WAYPOINT_LANDABLE_SIZE,
    N_("Landable size"),
    N_("A percentage to select the size landables are displayed on the map."),
    "OverrideWaypointLandableSize",
    ProfileKeys::AppLandableRenderingScale,
    {.waypoints = WaypointsBundleField::LANDABLE_SIZE},
    ProfileWireFormat::INT, 100, 50, 200, 10),
  PageSettingCatalog::CatalogBool(
    PageSettingId::WAYPOINT_SCALE_RUNWAY_LENGTH,
    N_("Scale runway length"),
    N_("[Off] Display fixed length for runways.\n"
       "[On] Scale displayed runway length based on real length."),
    "OverrideWaypointScaleRunwayLength",
    ProfileKeys::AppScaleRunwayLength,
    {.waypoints = WaypointsBundleField::SCALE_RUNWAY_LENGTH}),
};

static_assert(ARRAY_SIZE(catalog) == PageSettingWaypointsCount,
              "Catalog size must match waypoints PageSettingId count");

struct FieldAccessor {
  int (*get)(const Bundle &) noexcept;
  void (*set)(Bundle &, int) noexcept;
};

PAGE_SETTING_FIELD_ENUM(LabelFormat, waypoint.display_text_type,
                          WaypointRendererSettings::DisplayTextType)
PAGE_SETTING_FIELD_ENUM(ArrivalHeight, waypoint.arrival_height_display,
                          WaypointRendererSettings::ArrivalHeightDisplay)
PAGE_SETTING_FIELD_ENUM(LabelStyle, waypoint.landable_render_mode, LabelShape)
PAGE_SETTING_FIELD_ENUM(LabelVisibility, waypoint.label_selection,
                          WaypointRendererSettings::LabelSelection)
PAGE_SETTING_FIELD_ENUM(LandableSymbols, waypoint.landable_style,
                          WaypointRendererSettings::LandableStyle)
PAGE_SETTING_FIELD_INT(IconScale, waypoint.map_waypoint_icon_scale)
PAGE_SETTING_FIELD_BOOL(DetailedLandables, waypoint.vector_landable_rendering)
PAGE_SETTING_FIELD_INT(LandableSize, waypoint.landable_rendering_scale)
PAGE_SETTING_FIELD_BOOL(ScaleRunwayLength, waypoint.scale_runway_length)

static constexpr FieldAccessor field_accessors[] = {
  PAGE_SETTING_FIELD_ROW(LabelFormat),
  PAGE_SETTING_FIELD_ROW(ArrivalHeight),
  PAGE_SETTING_FIELD_ROW(LabelStyle),
  PAGE_SETTING_FIELD_ROW(LabelVisibility),
  PAGE_SETTING_FIELD_ROW(LandableSymbols),
  PAGE_SETTING_FIELD_ROW(IconScale),
  PAGE_SETTING_FIELD_ROW(DetailedLandables),
  PAGE_SETTING_FIELD_ROW(LandableSize),
  PAGE_SETTING_FIELD_ROW(ScaleRunwayLength),
};

static_assert(ARRAY_SIZE(field_accessors) ==
              unsigned(WaypointsBundleField::COUNT),
              "Waypoints field accessors must match WaypointsBundleField::COUNT");

[[nodiscard]]
WaypointsBundleField
FieldFromDescriptor(const PageSettingDescriptor &desc) noexcept
{
  return desc.bundle_field.waypoints;
}

[[nodiscard]]
int
GetBundleField(const Bundle &bundle, WaypointsBundleField field) noexcept
{
  return field_accessors[unsigned(field)].get(bundle);
}

void
SetBundleField(Bundle &bundle, WaypointsBundleField field,
               int value) noexcept
{
  field_accessors[unsigned(field)].set(bundle, value);
}

using Impl = PageSettingModuleImpl::Module<
  Bundle, WaypointsBundleField, catalog, PageSettingWaypointsCount,
  PageSettingWaypointsStart, FieldFromDescriptor,
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
  bundle.waypoint = CommonInterface::GetMapSettings().waypoint;
}

void
ApplyLive(const Bundle &bundle) noexcept
{
  CommonInterface::SetMapSettings().waypoint = bundle.waypoint;
}

void
LoadGlobal(Bundle &bundle) noexcept
{
  bundle.waypoint.SetDefaults();
  bundle.waypoint.LoadFromProfile();
}

bool
SaveGlobal(const Bundle &current, const Bundle &initial) noexcept
{
  return Impl::SaveGlobalBundle(current, initial);
}

} // namespace WaypointsDisplaySetting
