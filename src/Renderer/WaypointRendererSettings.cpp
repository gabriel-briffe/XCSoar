// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WaypointRendererSettings.hpp"

#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "Projection/MapWindowProjection.hpp"
#include "Waypoints/WaypointMapFilterProfile.hpp"

void
WaypointRendererSettings::LoadFromProfile() noexcept
{
  using namespace Profile;

  // NOTE: WaypointLabelSelection must be loaded after this code
  GetEnum(ProfileKeys::DisplayText, display_text_type);
  if (display_text_type == DisplayTextType::OBSOLETE_DONT_USE_NAMEIFINTASK) {
    display_text_type = DisplayTextType::NAME;
    label_selection = LabelSelection::TASK;
  } else if (display_text_type == DisplayTextType::OBSOLETE_DONT_USE_NUMBER)
    display_text_type = DisplayTextType::NAME;

  GetEnum(ProfileKeys::WaypointLabelSelection, label_selection);
  GetEnum(ProfileKeys::WaypointArrivalHeightDisplay, arrival_height_display);
  GetEnum(ProfileKeys::WaypointLabelStyle, landable_render_mode);

  GetEnum(ProfileKeys::AppIndLandable, landable_style);
  Get(ProfileKeys::AppUseSWLandablesRendering, vector_landable_rendering);
  Get(ProfileKeys::AppScaleRunwayLength, scale_runway_length);
  Get(ProfileKeys::AppLandableRenderingScale, landable_rendering_scale);
  Get(ProfileKeys::MapWaypointIconScale, map_waypoint_icon_scale);

  WaypointMapFilterProfile::Load(*this);
}

bool
WaypointRendererSettings::IsWaypointDisplayed(const Waypoint &waypoint) const noexcept
{
  return IsTypeDisplayed(waypoint.type);
}

bool
IsMapWaypointVisible(const Waypoint &waypoint,
                       const WaypointRendererSettings &settings,
                       const MapWindowProjection &projection,
                       bool in_task) noexcept
{
  if (!settings.IsWaypointDisplayed(waypoint))
    return false;

  if (!projection.WaypointInScaleFilter(waypoint) && !in_task)
    return false;

  return true;
}

void
WaypointRendererSettings::SaveTypeDisplay(Waypoint::Type type,
                                          bool display) noexcept
{
  const unsigned type_index = unsigned(type);
  if (type_index >= WAYPOINT_TYPE_COUNT)
    return;

  display_types[type_index] = display;
  WaypointMapFilterProfile::SaveTypeDisplay(type_index, display);
}
