// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Waypoints/WaypointsDisplaySetting.hpp"

#include "Interface.hpp"
#include "Language/Language.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingFieldAccessors.hpp"
#include "PageSettingFilterCatalog.hpp"
#include "PageSettingModuleImpl.hpp"
#include "PageSettingProfile.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Waypoints/WaypointMapFilterProfile.hpp"
#include "Waypoints/WaypointsDisplayChoices.hpp"
#include "Waypoints/WaypointMapFilterTypes.hpp"
#include "util/Macros.hpp"
#include "util/StringFormat.hpp"

#include <cassert>
#include <cstdint>

namespace WaypointsDisplaySetting {

void ReadLive(Bundle &) noexcept;
void ApplyLive(const Bundle &) noexcept;

namespace {

using Bundle = WaypointsDisplaySetting::Bundle;

static_assert(PageSettingWaypointsBaseCount == 9,
              "Waypoints base catalog size mismatch");
static_assert(PageSettingWaypointsCount ==
              PageSettingWaypointsBaseCount +
              PageSettingWaypointTypeFilterCount,
              "Waypoints catalog size mismatch");
static_assert(WAYPOINT_MAP_FILTER_TYPE_COUNT ==
              PageSettingWaypointMapFilterTypeCount,
              "Waypoint filter catalog must match filter type table");

static constexpr PageSettingDescriptor base_catalog[] = {
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

static_assert(ARRAY_SIZE(base_catalog) == PageSettingWaypointsBaseCount,
              "Base catalog size must match PageSettingWaypointsBaseCount");

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

static char filter_override_keys[PageSettingWaypointTypeFilterCount][40];
static char type_profile_keys[PageSettingWaypointTypeFilterCount][32];
static PageSettingDescriptor catalog[PageSettingWaypointsCount];
static PageSettingId filter_dialog_order[PageSettingWaypointTypeFilterCount];
static bool catalog_ready = false;

[[nodiscard]]
const char *
FilterDialogLabel(PageSettingId id) noexcept
{
  return PageSettingCatalog::GettextOptional(
    catalog[unsigned(id) - PageSettingWaypointsStart].label);
}

void
InitFilterDialogOrder() noexcept
{
  PageSettingFilterCatalog::FillConsecutiveIds(
    filter_dialog_order,
    PageSettingId::WAYPOINT_TYPE_FILTER_BEGIN,
    PageSettingWaypointTypeFilterCount);

  PageSettingFilterCatalog::InitSortedOrder(
    filter_dialog_order, FilterDialogRowCount(), FilterDialogLabel);
}

void
EnsureCatalog() noexcept
{
  if (catalog_ready)
    return;

  PageSettingFilterCatalog::CopyBase(catalog, base_catalog,
                                     PageSettingWaypointsBaseCount);

  for (unsigned i = 0; i < PageSettingWaypointTypeFilterCount; ++i) {
    const Waypoint::Type type = waypoint_map_filter_types[i];
    const PageSettingId id =
      PageSettingId(unsigned(PageSettingId::WAYPOINT_TYPE_FILTER_BEGIN) + i);

    StringFormat(filter_override_keys[i], sizeof(filter_override_keys[i]),
                 "OverrideWaypointTypeDisplay%u", unsigned(type));
    WaypointMapFilterProfile::FormatTypeDisplayKey(
      type_profile_keys[i], sizeof(type_profile_keys[i]), unsigned(type));

    catalog[PageSettingWaypointsBaseCount + i] =
      PageSettingFilterCatalog::MakeBoolFilter(
        id,
        GetWaypointMapFilterTypeName(type),
        N_("Display this waypoint type on the map."),
        filter_override_keys[i],
        type_profile_keys[i],
        {.waypoints = WaypointsBundleField::TYPE_FILTER});
  }

  InitFilterDialogOrder();
  catalog_ready = true;
}

[[nodiscard]]
WaypointsBundleField
FieldFromDescriptor(const PageSettingDescriptor &desc) noexcept
{
  assert(unsigned(desc.bundle_field.waypoints) <
         unsigned(WaypointsBundleField::COUNT));
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

using BaseImpl = PageSettingModuleImpl::Module<
  Bundle, WaypointsBundleField, catalog, PageSettingWaypointsBaseCount,
  PageSettingWaypointsStart, FieldFromDescriptor,
  GetBundleField, SetBundleField, ReadLive, ApplyLive>;

[[nodiscard]]
int
GetValueImpl(const Bundle &bundle, PageSettingId id) noexcept
{
  if (IsTypeFilter(id))
    return bundle.waypoint.display_types[unsigned(TypeFromFilterId(id))] ? 1 : 0;

  return BaseImpl::GetValue(bundle, id);
}

void
SetValueImpl(Bundle &bundle, PageSettingId id, int value) noexcept
{
  EnsureCatalog();
  if (!PageSettingCatalog::IsValidValue(
        catalog[unsigned(id) - PageSettingWaypointsStart], value))
    return;

  if (IsTypeFilter(id)) {
    bundle.waypoint.display_types[unsigned(TypeFromFilterId(id))] = value != 0;
    return;
  }

  BaseImpl::SetValue(bundle, id, value);
}

[[nodiscard]]
int
LoadGlobalImpl(PageSettingId id) noexcept
{
  if (IsTypeFilter(id))
    return WaypointMapFilterProfile::LoadTypeDisplay(
      unsigned(TypeFromFilterId(id))) ? 1 : 0;

  EnsureCatalog();
  return PageSettingProfile::Load(
    catalog[unsigned(id) - PageSettingWaypointsStart]);
}

void
SaveGlobalImpl(PageSettingId id, int value) noexcept
{
  EnsureCatalog();
  if (!PageSettingCatalog::IsValidValue(
        catalog[unsigned(id) - PageSettingWaypointsStart], value))
    return;

  if (IsTypeFilter(id)) {
    WaypointMapFilterProfile::SaveTypeDisplay(
      unsigned(TypeFromFilterId(id)), value != 0);
    return;
  }

  PageSettingProfile::Save(
    catalog[unsigned(id) - PageSettingWaypointsStart], value);
}

using Dyn = PageSettingModuleImpl::DynamicModule<
  Bundle,
  PageSettingWaypointsCount,
  PageSettingWaypointsStart,
  PageSettingAirspaceStart,
  catalog,
  EnsureCatalog,
  GetValueImpl,
  SetValueImpl,
  LoadGlobalImpl,
  SaveGlobalImpl,
  ReadLive,
  ApplyLive>;

} // namespace

PageSettingId
FilterDialogRowId(unsigned row) noexcept
{
  assert(row < FilterDialogRowCount());
  EnsureCatalog();
  return filter_dialog_order[row];
}

unsigned
Count() noexcept
{
  return Dyn::Count();
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  return Dyn::Get(id);
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  return Dyn::Get(index);
}

bool
IsValidValue(PageSettingId id, int value) noexcept
{
  return Dyn::IsValidValue(id, value);
}

int
GetValue(const Bundle &bundle, PageSettingId id) noexcept
{
  return GetValueImpl(bundle, id);
}

void
SetValue(Bundle &bundle, PageSettingId id, int value) noexcept
{
  SetValueImpl(bundle, id, value);
}

int
GetLive(PageSettingId id) noexcept
{
  return Dyn::GetLive(id);
}

void
SetLive(PageSettingId id, int value) noexcept
{
  Dyn::SetLive(id, value);
}

int
LoadGlobal(PageSettingId id) noexcept
{
  return Dyn::LoadGlobal(id);
}

void
SaveGlobal(PageSettingId id, int value) noexcept
{
  Dyn::SaveGlobal(id, value);
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
  return Dyn::SaveGlobalBundle(current, initial);
}

} // namespace WaypointsDisplaySetting
