// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Engine/Airspace/AirspaceClass.hpp"

#include <cstdint>
#include <type_traits>

struct TrailSettings;
struct WaypointRendererSettings;

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
 * [#AIRSPACE_CLASS_FILTER_BEGIN, #COUNT): one entry per #AirspaceClass
 * except OTHER (same skip as the Airspace Filter dialog).
 */
enum class PageSettingId : uint8_t {
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

  AIRSPACE_DISPLAY,
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

  COUNT = AIRSPACE_CLASS_FILTER_BEGIN + (AIRSPACECLASSCOUNT - 1)
};

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
constexpr unsigned PageSettingAirspaceCount =
  unsigned(PageSettingId::COUNT) - PageSettingAirspaceStart;
constexpr unsigned PageSettingAirspaceClassFilterCount =
  AIRSPACECLASSCOUNT - 1;
constexpr unsigned PageSettingAirspaceBaseCount =
  PageSettingAirspaceCount - PageSettingAirspaceClassFilterCount;

/**
 * Sparse per-page setting overrides.  Only entries present in #items
 * appear in the Pages editor; value #INHERIT means "use the global
 * setting" while keeping the field on this page.
 *
 * #MAX_ITEMS caps overrides per page (typical use is small; the catalog
 * may grow much larger).  Keep #RowFormWidget::MAX_ROWS >= MAX_ITEMS.
 */
struct PageSettingOverrides {
  static constexpr unsigned MAX_ITEMS = 128;

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
/* Keep RowFormWidget::MAX_ROWS >= PageSettingOverrides::MAX_ITEMS
   (asserted in Dialogs/Settings/Panels/PagesConfigPanel.cpp). */

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
 * settings (no map notify).  Pair with #PageSettingApplyPageOverrides
 * then #PageSettingNotifyLive.
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

/** Push live MapSettings to the map (one FullRedraw). */
void
PageSettingNotifyLive() noexcept;
