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

/** Field within #MapDisplaySetting::Bundle. */
enum class MapBundleField : uint8_t {
  CRUISE_ORIENTATION,
  CIRCLING_ORIENTATION,
  CIRCLING_ZOOM,
  MAP_SHIFT_BIAS,
  GLIDER_SCREEN_POSITION,

  COUNT
};

/**
 * Active bundle field for a catalog row.  #PageSettingGroup selects which
 * member is valid; add a member here when introducing a new settings group.
 */
union PageSettingBundleField {
  TerrainBundleField terrain;
  MapBundleField map;
};

/** How a catalog value is stored in the profile file. */
enum class ProfileWireFormat : uint8_t {
  BOOL,
  UNSIGNED,
  UINT8_SLOPE,
  UINT8_CONTOURS,
  SHORT_PERCENT,
  UINT8_MAP_ORIENTATION,
  UINT8_MAP_SHIFT_BIAS,
  INT,

  COUNT
};

/**
 * One catalog entry: UI metadata and profile keys.
 * Get/set logic lives in each group's *DisplaySetting module.
 *
 * Adding a group: extend #PageSettingGroup and #PageSettingBundleField,
 * add #PageSettingId values, implement *DisplaySetting.cpp catalog rows
 * using {.terrain = ...} or {.map = ...} (or the new union member).
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
};
