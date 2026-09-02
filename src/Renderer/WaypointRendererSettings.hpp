// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Engine/Waypoint/Waypoint.hpp"
#include "LabelShape.hpp"
#include "Waypoints/WaypointMapFilterTypes.hpp"

#include <cstdint>

struct WaypointRendererSettings {
  /** What type of text to draw next to the waypoint icon */
  enum class DisplayTextType : uint8_t {
    NAME = 0,
    OBSOLETE_DONT_USE_NUMBER,
    FIRST_FIVE,
    NONE,
    FIRST_THREE,
    OBSOLETE_DONT_USE_NAMEIFINTASK,
    FIRST_WORD,
    SHORT_NAME,
  } display_text_type;

  /** Which arrival height to display next to waypoint labels */

  enum class ArrivalHeightDisplay : uint8_t {
    NONE = 0,
    GLIDE,
    TERRAIN,
    GLIDE_AND_TERRAIN,
    REQUIRED_GR,
    REQUIRED_GR_AND_TERRAIN,
  } arrival_height_display;

  /** What type of waypoint labels to render */
  enum class LabelSelection : uint8_t {
    ALL,
    TASK_AND_LANDABLE,
    TASK,
    NONE,
    TASK_AND_AIRFIELD,
  } label_selection;

  /** What type of waypoint labels to render */
  LabelShape landable_render_mode;

  enum class LandableStyle : uint8_t {
    PURPLE_CIRCLE,
    BW,
    TRAFFIC_LIGHTS,
  } landable_style;

  bool vector_landable_rendering;

  bool scale_runway_length;

  int landable_rendering_scale;

  /**
   * Map waypoint symbol size in percent of intrinsic icon / vector scale
   * (50–200; Configuration → Map display → Waypoints).
   */
  int map_waypoint_icon_scale;

  /** Per #Waypoint::Type map symbol filter (indexed by type). */
  bool display_types[WAYPOINT_TYPE_COUNT];

  /**
   * When false, hide airports whose short name is not exactly four
   * characters (typical ICAO code length).
   */
  bool display_non_icao_airports;

  [[gnu::pure]]
  bool IsTypeDisplayed(Waypoint::Type type) const noexcept {
    const unsigned i = unsigned(type);
    return i >= WAYPOINT_TYPE_COUNT || display_types[i];
  }

  [[gnu::pure]]
  bool IsWaypointDisplayed(const Waypoint &waypoint) const noexcept;

  void SetDefaults() noexcept {
    display_text_type = DisplayTextType::SHORT_NAME;
    arrival_height_display = ArrivalHeightDisplay::GLIDE;
    label_selection = LabelSelection::ALL;
    landable_render_mode = LabelShape::ROUNDED_BLACK;

    landable_style = LandableStyle::PURPLE_CIRCLE;
    vector_landable_rendering = true;
    scale_runway_length = false;
    landable_rendering_scale = 100;
    map_waypoint_icon_scale = 100;

    for (bool &display : display_types)
      display = true;

    display_non_icao_airports = true;
  }

  void LoadFromProfile() noexcept;

  /** Persist one type's display flag to the profile. */
  void SaveTypeDisplay(Waypoint::Type type, bool display) noexcept;

  void SaveNonIcaoAirportsDisplay(bool display) noexcept;
};

class MapWindowProjection;

[[nodiscard]]
bool
IsMapWaypointVisible(const Waypoint &waypoint,
                     const WaypointRendererSettings &settings,
                     const MapWindowProjection &projection,
                     bool in_task) noexcept;
