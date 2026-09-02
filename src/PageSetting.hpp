// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Engine/Airspace/AirspaceClass.hpp"
#include "Waypoints/WaypointMapFilterTypes.hpp"

#include <cstdint>
#include <type_traits>

struct TrailSettings;
struct WaypointRendererSettings;
struct AirspaceRendererSettings;

constexpr unsigned PageSettingWaypointMapFilterTypeCount =
  WAYPOINT_MAP_FILTER_TYPE_COUNT;

enum class PageSettingGroup : uint8_t {
  TERRAIN,
  ORIENTATION,
  ELEMENTS,
  WAYPOINTS,
  AIRSPACE,

  COUNT
};

/**
 * Identifiers for settings that may be overridden per page.
 * The registry in PageSetting.cpp is the catalog (terrain, orientation,
 * elements, waypoints, then airspace).
 *
 * Airspace class filters occupy
 * [#AIRSPACE_CLASS_FILTER_BEGIN, #AIRSPACE_CLASS_FILL_COLOR_BEGIN).
 * Class fill/border colours, border width, and fill mode follow
 * (one each per #AirspaceClass except OTHER).
 */
enum class PageSettingId : uint16_t {
  TERRAIN_ENABLE = 0,
  TOPOGRAPHY_ENABLE,
  TERRAIN_COLORS,
  TERRAIN_SLOPE_SHADING,
  TERRAIN_CONTRAST,
  TERRAIN_BRIGHTNESS,
  TERRAIN_CONTOURS,

  CRUISE_ORIENTATION,
  CIRCLING_ORIENTATION,
  CIRCLING_ZOOM,
  MAP_SHIFT_BIAS,
  GLIDER_SCREEN_POSITION,

  GROUND_TRACK,
  FLARM_TRAFFIC,
  FADE_TRAFFIC,
  TRAIL_LENGTH,
  TRAIL_DRIFT,
  TRAIL_TYPE,
  TRAIL_SCALED,
  DETOUR_COST_MARKERS,
  AIRCRAFT_SYMBOL,
  WIND_ARROW_STYLE,
  ONLINE_TRAFFIC_MAP_MODE,
  DISTANCE_RINGS,

  WAYPOINT_LABEL_FORMAT,
  WAYPOINT_ARRIVAL_HEIGHT,
  WAYPOINT_LABEL_STYLE,
  WAYPOINT_LABEL_VISIBILITY,
  WAYPOINT_LANDABLE_SYMBOLS,
  WAYPOINT_ICON_SCALE,
  WAYPOINT_DETAILED_LANDABLES,
  WAYPOINT_LANDABLE_SIZE,
  WAYPOINT_SCALE_RUNWAY_LENGTH,

  WAYPOINT_TYPE_FILTER_BEGIN,

  AIRSPACE_DISPLAY =
    WAYPOINT_TYPE_FILTER_BEGIN + PageSettingWaypointMapFilterTypeCount,
  AIRSPACE_LABEL_VISIBILITY,
  AIRSPACE_SHOW_NOTAM_LABELS,
  AIRSPACE_CLIP_ALTITUDE,
  AIRSPACE_MARGIN,
  AIRSPACE_WARNINGS,
  AIRSPACE_WARNING_DIALOG,
  AIRSPACE_WARNING_TIME,
  AIRSPACE_REPETITIVE_SOUND,
  AIRSPACE_ACKNOWLEDGE_TIME,
  AIRSPACE_BLACK_OUTLINE,
  AIRSPACE_FILL_MODE,
  AIRSPACE_TRANSPARENCY,

  AIRSPACE_CLASS_FILTER_BEGIN,

  AIRSPACE_CLASS_FILL_COLOR_BEGIN =
    AIRSPACE_CLASS_FILTER_BEGIN + (AIRSPACECLASSCOUNT - 1),

  AIRSPACE_CLASS_BORDER_COLOR_BEGIN =
    AIRSPACE_CLASS_FILL_COLOR_BEGIN + (AIRSPACECLASSCOUNT - 1),

  AIRSPACE_CLASS_BORDER_WIDTH_BEGIN =
    AIRSPACE_CLASS_BORDER_COLOR_BEGIN + (AIRSPACECLASSCOUNT - 1),

  AIRSPACE_CLASS_FILL_MODE_BEGIN =
    AIRSPACE_CLASS_BORDER_WIDTH_BEGIN + (AIRSPACECLASSCOUNT - 1),

  COUNT = AIRSPACE_CLASS_FILL_MODE_BEGIN + (AIRSPACECLASSCOUNT - 1)
};

static_assert(unsigned(PageSettingId::COUNT) <= 65535,
              "PageSettingId must fit in uint16_t");

constexpr unsigned PageSettingTerrainCount =
  unsigned(PageSettingId::CRUISE_ORIENTATION);
constexpr unsigned PageSettingOrientationStart =
  unsigned(PageSettingId::CRUISE_ORIENTATION);
constexpr unsigned PageSettingElementsStart =
  unsigned(PageSettingId::GROUND_TRACK);
constexpr unsigned PageSettingWaypointsStart =
  unsigned(PageSettingId::WAYPOINT_LABEL_FORMAT);
constexpr unsigned PageSettingAirspaceStart =
  unsigned(PageSettingId::AIRSPACE_DISPLAY);
constexpr unsigned PageSettingOrientationCount =
  PageSettingElementsStart - PageSettingOrientationStart;
constexpr unsigned PageSettingElementsCount =
  PageSettingWaypointsStart - PageSettingElementsStart;
constexpr unsigned PageSettingWaypointsCount =
  PageSettingAirspaceStart - PageSettingWaypointsStart;
constexpr unsigned PageSettingWaypointTypeFilterCount =
  PageSettingWaypointMapFilterTypeCount;
constexpr unsigned PageSettingWaypointsBaseCount =
  unsigned(PageSettingId::WAYPOINT_TYPE_FILTER_BEGIN) -
  PageSettingWaypointsStart;
constexpr unsigned PageSettingAirspaceCount =
  unsigned(PageSettingId::COUNT) - PageSettingAirspaceStart;
constexpr unsigned PageSettingAirspaceClassFilterCount =
  AIRSPACECLASSCOUNT - 1;
constexpr unsigned PageSettingAirspaceClassFillColorCount =
  AIRSPACECLASSCOUNT - 1;
constexpr unsigned PageSettingAirspaceClassBorderColorCount =
  AIRSPACECLASSCOUNT - 1;
constexpr unsigned PageSettingAirspaceClassBorderWidthCount =
  AIRSPACECLASSCOUNT - 1;
constexpr unsigned PageSettingAirspaceClassFillModeCount =
  AIRSPACECLASSCOUNT - 1;
constexpr unsigned PageSettingAirspaceBaseCount =
  PageSettingAirspaceCount -
  PageSettingAirspaceClassFilterCount -
  PageSettingAirspaceClassFillColorCount -
  PageSettingAirspaceClassBorderColorCount -
  PageSettingAirspaceClassBorderWidthCount -
  PageSettingAirspaceClassFillModeCount;

/**
 * Sparse per-page setting overrides.  Only entries present in #items
 * appear in the Pages editor; value #INHERIT means "use the global
 * setting" while keeping the field on this page.
 *
 * #MAX_ITEMS caps overrides per page (typical use is small; the catalog
 * may grow much larger).
 */
struct PageSettingOverrides {
  static constexpr unsigned MAX_ITEMS = 256;

  /** Sentinel: follow the global / profile value. */
  static constexpr int INHERIT = -1;

  struct Item {
    PageSettingId id;
    int value;
  };

  Item items[MAX_ITEMS];
  unsigned n_items;

  constexpr void Clear() noexcept {
    n_items = 0;
  }

  [[nodiscard]]
  constexpr bool IsEmpty() const noexcept {
    return n_items == 0;
  }

  [[nodiscard]]
  bool Contains(PageSettingId id) const noexcept;

  [[nodiscard]]
  int *FindValue(PageSettingId id) noexcept;

  [[nodiscard]]
  const int *FindValue(PageSettingId id) const noexcept;

  /**
   * Add @p id if missing.  New entries default to #INHERIT.
   * @return true when a new entry was added
   */
  bool Add(PageSettingId id, int value = INHERIT) noexcept;

  bool Remove(PageSettingId id) noexcept;

  void SetValue(PageSettingId id, int value) noexcept;

  [[nodiscard]]
  constexpr bool operator==(const PageSettingOverrides &other) const noexcept {
    if (n_items != other.n_items)
      return false;
    for (unsigned i = 0; i < n_items; ++i)
      if (items[i].id != other.items[i].id ||
          items[i].value != other.items[i].value)
        return false;
    return true;
  }

  [[nodiscard]]
  constexpr bool operator!=(const PageSettingOverrides &other) const noexcept {
    return !(*this == other);
  }
};

static_assert(std::is_trivial_v<PageSettingOverrides>);

struct PageSettingDescriptor;

/**
 * Catalog of page-applicable settings (labels, choices, apply).
 */
namespace PageSettingRegistry {

[[nodiscard]]
unsigned
Count() noexcept;

[[nodiscard]]
const PageSettingDescriptor &
Get(PageSettingId id) noexcept;

[[nodiscard]]
const PageSettingDescriptor &
Get(unsigned index) noexcept;

[[nodiscard]]
bool
IsValidValue(const PageSettingDescriptor &desc, int value) noexcept;

[[nodiscard]]
unsigned
Count(PageSettingGroup group) noexcept;

[[nodiscard]]
const PageSettingDescriptor &
Get(PageSettingGroup group, unsigned index) noexcept;

} // namespace PageSettingRegistry

/** Read the global profile value for @p id. */
[[nodiscard]]
int
PageSettingGet(PageSettingId id) noexcept;

/**
 * Read @p id for @p page_index: page override if present, else global
 * profile value.
 */
[[nodiscard]]
int
PageSettingGet(PageSettingId id, unsigned page_index) noexcept;

/** Write global profile + live map and notify. */
void
PageSettingSet(PageSettingId id, int value) noexcept;

/** Write a per-page override only (no live apply). */
void
PageSettingSet(PageSettingId id, int value, unsigned page_index) noexcept;

/**
 * Reload live MapSettings from the global profile for all catalog
 * settings (no map notify).  Prefer #PageSettingApplyDisplaySettings
 * on page switches.
 */
void
PageSettingApplyGlobalBaseline() noexcept;

/**
 * Apply sparse overrides for @p page_index onto live MapSettings
 * (no map notify).  #INHERIT entries are skipped.
 */
void
PageSettingApplyPageOverrides(unsigned page_index) noexcept;

/**
 * Restore previous page overrides (when known) or full global baseline,
 * then apply @p page_index overrides.  No map notify.
 */
void
PageSettingApplyDisplaySettings(unsigned page_index) noexcept;

/**
 * Rebuild trail pens/brushes when @p before differs from live trail
 * settings (type or scaled width).  Trail rendering reads #MapSettings
 * for behaviour but colours come from #TrailLook.
 */
void
PageSettingReinitialiseTrailLookIfChanged(const TrailSettings &before) noexcept;

/**
 * Rebuild waypoint look when @p before.landable_style differs from live
 * map settings.
 */
void
PageSettingReinitialiseWaypointLookIfChanged(
  const WaypointRendererSettings &before) noexcept;

/**
 * Rebuild airspace class pens/brushes when @p before class colours
 * differ from live map settings.
 */
void
PageSettingReinitialiseAirspaceLookIfChanged(
  const AirspaceRendererSettings &before) noexcept;

/** Push live MapSettings to the map (one FullRedraw). */
void
PageSettingNotifyLive() noexcept;
