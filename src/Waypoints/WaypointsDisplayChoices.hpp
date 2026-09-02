// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Shared Map Display → Waypoints choice lists (Config panel and page
 * overrides).
 */

#include "Form/DataField/Enum.hpp"
#include "Language/Language.hpp"
#include "Renderer/LabelShape.hpp"
#include "Renderer/WaypointRendererSettings.hpp"

static constexpr StaticEnumChoice waypoint_label_format_choices[] = {
  { WaypointRendererSettings::DisplayTextType::NAME,
    N_("Full name"),
    N_("The full name of each waypoint is displayed.") },
  { WaypointRendererSettings::DisplayTextType::FIRST_WORD,
    N_("First word of name"),
    N_("The first word of the waypoint name is displayed.") },
  { WaypointRendererSettings::DisplayTextType::FIRST_THREE,
    N_("First 3 letters"),
    N_("The first 3 letters of the waypoint name are displayed.") },
  { WaypointRendererSettings::DisplayTextType::FIRST_FIVE,
    N_("First 5 letters"),
    N_("The first 5 letters of the waypoint name are displayed.") },
  { WaypointRendererSettings::DisplayTextType::NONE,
    N_("None"), N_("No waypoint name is displayed.") },
  { WaypointRendererSettings::DisplayTextType::SHORT_NAME,
    N_("Short Name"),
    N_("The short name of each waypoint is displayed. If unavailable, "
       "the first five letters of the full name are displayed.") },
  nullptr
};

static constexpr StaticEnumChoice waypoint_arrival_height_choices[] = {
  { WaypointRendererSettings::ArrivalHeightDisplay::NONE,
    N_("None"),
    N_("No arrival height is displayed.") },
  { WaypointRendererSettings::ArrivalHeightDisplay::GLIDE,
    N_("Straight glide"),
    N_("Straight glide arrival height (no terrain is considered).") },
  { WaypointRendererSettings::ArrivalHeightDisplay::TERRAIN,
    N_("Terrain avoidance glide"),
    N_("Arrival height considering terrain avoidance. "
       "Requires \"Reach mode: Turning\" in \"Glide Computer > Route\" "
       "settings.") },
  { WaypointRendererSettings::ArrivalHeightDisplay::GLIDE_AND_TERRAIN,
    N_("Straight & terrain glide"),
    N_("Both arrival heights are displayed. "
       "Requires \"Reach mode: Turning\" in \"Glide Computer > Route\" "
       "settings.") },
  { WaypointRendererSettings::ArrivalHeightDisplay::REQUIRED_GR,
    N_("Required glide ratio") },
  { WaypointRendererSettings::ArrivalHeightDisplay::REQUIRED_GR_AND_TERRAIN,
    N_("Required GR & terrain glide"),
    N_("Both Required glide ratio and terrain avoidance height are "
       "displayed. Requires \"Reach mode: Turning\" in \"Glide Computer > "
       "Route\" settings.") },
  nullptr
};

static constexpr StaticEnumChoice waypoint_label_style_choices[] = {
  { LabelShape::ROUNDED_BLACK, N_("Rounded rectangle") },
  { LabelShape::OUTLINED_INVERTED, N_("Outlined") },
  nullptr
};

static constexpr StaticEnumChoice waypoint_label_visibility_choices[] = {
  { WaypointRendererSettings::LabelSelection::ALL,
    N_("All"), N_("All labels will be displayed.") },
  { WaypointRendererSettings::LabelSelection::TASK_AND_AIRFIELD,
    N_("Task waypoints & airfields"),
    N_("All waypoints part of a task and all airfields will be displayed.") },
  { WaypointRendererSettings::LabelSelection::TASK_AND_LANDABLE,
    N_("Task waypoints & landables"),
    N_("All waypoints part of a task and all landables will be displayed.") },
  { WaypointRendererSettings::LabelSelection::TASK,
    N_("Task waypoints"),
    N_("All waypoints part of a task will be displayed.") },
  { WaypointRendererSettings::LabelSelection::NONE,
    N_("None"), N_("No labels will be displayed.") },
  nullptr
};

static constexpr StaticEnumChoice waypoint_landable_style_choices[] = {
  { WaypointRendererSettings::LandableStyle::PURPLE_CIRCLE,
    N_("Purple circle"),
    N_("Airports and outlanding fields are displayed as purple circles. "
       "If the waypoint is reachable a bigger green circle is added behind "
       "the purple one. If the waypoint is blocked by a mountain the green "
       "circle will be red instead.") },
  { WaypointRendererSettings::LandableStyle::BW,
    N_("B/W"),
    N_("Airports and outlanding fields are displayed in white/grey. If the "
       "waypoint is reachable the color is changed to green. If the waypoint "
       "is blocked by a mountain the color is changed to red instead.") },
  { WaypointRendererSettings::LandableStyle::TRAFFIC_LIGHTS,
    N_("Traffic lights"),
    N_("Airports and outlanding fields are displayed in the colors of a "
       "traffic light. Green if reachable, Orange if blocked by mountain "
       "and red if not reachable at all.") },
  nullptr
};
