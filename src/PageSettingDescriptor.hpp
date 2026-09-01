// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSetting.hpp"
#include "Form/DataField/Enum.hpp"

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
};

/** How a catalog value is stored in the profile file. */
enum class ProfileWireFormat : uint8_t {
  BOOL,
  UNSIGNED,
  UINT8_SLOPE,
  UINT8_CONTOURS,
  SHORT_PERCENT,
};

/**
 * One catalog entry: UI metadata and profile keys.
 * Get/set logic lives in TerrainDisplaySetting.
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

  TerrainBundleField bundle_field;

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
