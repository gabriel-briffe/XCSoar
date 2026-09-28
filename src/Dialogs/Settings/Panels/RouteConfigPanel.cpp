// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RouteConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

std::unique_ptr<Widget>
CreateRouteConfigPanel()
{
  const ComputerSettings &settings_computer =
    CommonInterface::GetComputerSettings();
  const RoutePlannerConfig &route_planner =
    settings_computer.task.route_planner;

  struct Fields {
    RoutePlannerConfig::Mode mode;
    bool allow_climb;
    bool use_ceiling;
    RoutePlannerConfig::ReachMode reach_mode;
    RoutePlannerConfig::Polar reach_polar;
    FeaturesSettings::FinalGlideTerrain final_glide_terrain;
  };

  auto fields = std::make_shared<Fields>(Fields{
    route_planner.mode,
    route_planner.allow_climb,
    route_planner.use_ceiling,
    route_planner.reach_calc_mode,
    route_planner.reach_polar_mode,
    settings_computer.features.final_glide_terrain,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

  static constexpr StaticEnumChoice route_mode_list[] = {
    { RoutePlannerConfig::Mode::NONE, N_("None"),
      N_("Neither airspace nor terrain is used for route planning.") },
    { RoutePlannerConfig::Mode::TERRAIN, N_("Terrain"),
      N_("Routes will avoid terrain.") },
    { RoutePlannerConfig::Mode::AIRSPACE, N_("Airspace"),
      N_("Routes will avoid airspace.") },
    { RoutePlannerConfig::Mode::BOTH, N_("Both"),
      N_("Routes will avoid airspace and terrain.") },
    nullptr
  };

  list->AddEnum(_("Route mode"), nullptr, route_mode_list, fields->mode);
  list->AddSwitch(_("Route climb"),
                  _("When enabled and MC is positive, route planning allows climbs between the aircraft "
                      "location and destination."),
                  fields->allow_climb, true,
                  [fields] {
                    return fields->mode != RoutePlannerConfig::Mode::NONE;
                  });
  list->AddSwitch(_("Route ceiling"),
                  _("When enabled, route planning climbs are limited to ceiling defined by greater of "
                      "current aircraft altitude plus 500 m and the thermal ceiling. If disabled, "
                      "climbs are unlimited."),
                  fields->use_ceiling, true,
                  [fields] {
                    return fields->mode != RoutePlannerConfig::Mode::NONE;
                  });

  static constexpr StaticEnumChoice turning_reach_list[] = {
    { RoutePlannerConfig::ReachMode::OFF, N_("Off"),
      N_("Reach calculations disabled.") },
    { RoutePlannerConfig::ReachMode::STRAIGHT, N_("Straight"),
      N_("The reach is from straight line paths from the glider.") },
    { RoutePlannerConfig::ReachMode::TURNING, N_("Turning"),
      N_("The reach is calculated allowing turns around terrain obstacles.") },
    nullptr
  };

  list->AddEnum(_("Reach mode"),
                _("How calculations are performed of the reach of the glider with respect to terrain."),
                turning_reach_list, fields->reach_mode);

  static constexpr StaticEnumChoice reach_polar_list[] = {
    { RoutePlannerConfig::Polar::TASK, N_("Task"),
      N_("Uses task glide polar.") },
    { RoutePlannerConfig::Polar::SAFETY, N_("Safety MC"),
      N_("Uses safety MacCready value.") },
    nullptr
  };

  list->AddEnum(_("Reach polar"),
                _("This determines the glide performance used in reach, landable arrival, abort and alternate calculations."),
                reach_polar_list, fields->reach_polar, true,
                [fields] {
                  return fields->reach_mode !=
                         RoutePlannerConfig::ReachMode::OFF;
                });

  static constexpr StaticEnumChoice final_glide_terrain_list[] = {
    { FeaturesSettings::FinalGlideTerrain::OFF, N_("Off"),
      N_("Disables the reach display.") },
    { FeaturesSettings::FinalGlideTerrain::TERRAIN_LINE, N_("Terrain line"),
      N_("Draws a dashed line at the terrain glide reach.") },
    { FeaturesSettings::FinalGlideTerrain::TERRAIN_SHADE, N_("Terrain shade"),
      N_("Shades terrain outside glide reach.") },
    { FeaturesSettings::FinalGlideTerrain::WORKING, N_("Working line"),
      N_("Draws a dashed line at the working glide reach.") },
    { FeaturesSettings::FinalGlideTerrain::WORKING_TERRAIN_LINE, N_("Working line, terrain line"),
      N_("Draws a dashed line at the working and terrain glide reaches.") },
    { FeaturesSettings::FinalGlideTerrain::WORKING_TERRAIN_SHADE, N_("Working line, terrain shade"),
      N_("Draws a dashed line at working, and shade terrain, glide reaches.") },
    nullptr
  };

  list->AddEnum(_("Reach display"), nullptr, final_glide_terrain_list,
                fields->final_glide_terrain, false,
                [fields] {
                  return fields->reach_mode !=
                         RoutePlannerConfig::ReachMode::OFF;
                });

  list->SetSaveCallback([fields](bool &changed) {
    ComputerSettings &settings_computer =
      CommonInterface::SetComputerSettings();
    RoutePlannerConfig &route_planner =
      settings_computer.task.route_planner;

    ConfigPanel::CommitSetting(changed, route_planner.mode, fields->mode,
                               ProfileKeys::RoutePlannerMode);
    ConfigPanel::CommitSetting(changed, route_planner.reach_polar_mode,
                               fields->reach_polar,
                               ProfileKeys::ReachPolarMode);
    ConfigPanel::CommitSetting(changed,
                               settings_computer.features.final_glide_terrain,
                               fields->final_glide_terrain,
                               ProfileKeys::FinalGlideTerrain);
    ConfigPanel::CommitSetting(changed, route_planner.allow_climb,
                               fields->allow_climb,
                               ProfileKeys::RoutePlannerAllowClimb);
    ConfigPanel::CommitSetting(changed, route_planner.use_ceiling,
                               fields->use_ceiling,
                               ProfileKeys::RoutePlannerUseCeiling);
    ConfigPanel::CommitSetting(changed, route_planner.reach_calc_mode,
                               fields->reach_mode, ProfileKeys::TurningReach);
    return true;
  });

  return list;
}
