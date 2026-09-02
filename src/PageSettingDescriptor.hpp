// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSetting.hpp"
#include "Form/DataField/Enum.hpp"

#include <cassert>
#include <string_view>

/**
 * Value shape for a catalog setting.  Storage remains #int
 * (bool 0/1, enum choice id, or integer range value).
 */
enum class PageSettingType : uint8_t {
  ENUM,
  BOOL,
  INTEGER,
};

/** Field within #TerrainDisplaySetting::Bundle. */
enum class TerrainBundleField : uint8_t {
  TERRAIN_ENABLE,
  TOPOGRAPHY_ENABLE,
  TERRAIN_RAMP,
  TERRAIN_SLOPE_SHADING,
  TERRAIN_CONTRAST,
  TERRAIN_BRIGHTNESS,
  TERRAIN_CONTOURS,

  COUNT
};

/** Field within #OrientationDisplaySetting::Bundle. */
enum class OrientationBundleField : uint8_t {
  CRUISE_ORIENTATION,
  CIRCLING_ORIENTATION,
  CIRCLING_ZOOM,
  MAP_SHIFT_BIAS,
  GLIDER_SCREEN_POSITION,

  COUNT
};

/** Field within #ElementsDisplaySetting::Bundle. */
enum class ElementsBundleField : uint8_t {
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

  COUNT
};

/** Field within #WaypointsDisplaySetting::Bundle. */
enum class WaypointsBundleField : uint8_t {
  LABEL_FORMAT,
  ARRIVAL_HEIGHT,
  LABEL_STYLE,
  LABEL_VISIBILITY,
  LANDABLE_SYMBOLS,
  ICON_SCALE,
  DETAILED_LANDABLES,
  LANDABLE_SIZE,
  SCALE_RUNWAY_LENGTH,

  COUNT,

  /** Catalog-only; handled by custom get/set (not in field accessors). */
  TYPE_FILTER,
  NON_ICAO_FILTER,
};

/** Field within #AirspaceDisplaySetting::Bundle. */
enum class AirspaceBundleField : uint8_t {
  DISPLAY,
  LABEL_VISIBILITY,
  SHOW_NOTAM_LABELS,
  CLIP_ALTITUDE,
  MARGIN,
  WARNINGS,
  WARNING_DIALOG,
  WARNING_TIME,
  REPETITIVE_SOUND,
  ACKNOWLEDGE_TIME,
  BLACK_OUTLINE,
  FILL_MODE,
  TRANSPARENCY,

  COUNT,

  /** Catalog-only; handled by custom get/set (not in field accessors). */
  CLASS_FILTER,
  CLASS_FILL_COLOR,
  CLASS_BORDER_COLOR,
};

/**
 * Active bundle field for a catalog row.  #PageSettingGroup selects which
 * member is valid; add a member here when introducing a new settings group.
 */
union PageSettingBundleField {
  TerrainBundleField terrain;
  OrientationBundleField orientation;
  ElementsBundleField elements;
  WaypointsBundleField waypoints;
  AirspaceBundleField airspace;
};

/** How a catalog value is stored in the profile file. */
enum class ProfileWireFormat : uint8_t {
  BOOL,
  UNSIGNED,
  SHORT_PERCENT,
  INT,
  UINT8_ENUM,

  COUNT
};

/**
 * One catalog entry: UI metadata and profile keys.
 * Get/set logic lives in each group's *DisplaySetting module.
 *
 * Adding a group: extend #PageSettingGroup and #PageSettingBundleField,
 * add #PageSettingId values, implement *DisplaySetting.cpp catalog rows
 * using {.terrain = ...}, {.orientation = ...}, {.elements = ...},
 * {.waypoints = ...}, or {.airspace = ...}.
 */
struct PageSettingDescriptor {
  PageSettingId id;

  PageSettingType type;

  /** UI label (N_(); gettext when showing). */
  const char *label;

  /** Help text for Config and Pages editors (N_()). */
  const char *help_global;

  /**
   * Profile key suffix after "PageN" for per-page overrides
   * (e.g. "OverrideTerrainColors").
   */
  const char *override_key;

  /** Global profile key (Map Display → profile). */
  std::string_view profile_key;

  PageSettingBundleField bundle_field;

  ProfileWireFormat profile_wire;

  /**
   * Default when the profile key is missing: 0/1 for #ProfileWireFormat::BOOL,
   * choice id for enums, byte 0..255 for #ProfileWireFormat::SHORT_PERCENT.
   */
  int profile_default;

  /**
   * Choice list for ENUM/BOOL editors.  nullptr for INTEGER (range
   * filled from int_*).
   */
  const StaticEnumChoice *choices;

  /** Inclusive range for INTEGER; unused (0) for ENUM/BOOL. */
  int int_min;
  int int_max;
  int int_step;

  /**
   * printf format for INTEGER choice labels (e.g. "%d %%", "%d m", "%d s").
   * nullptr means "%d %%".
   */
  const char *int_format = nullptr;

  /**
   * Optional section for the Pages → Custom settings Add picker (N_()).
   * nullptr continues the previous section (no header).  When the section
   * string changes, the picker inserts a non-selectable header.
   */
  const char *section = nullptr;
};
