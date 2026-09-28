// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SafetyFactorsConfigPanel.hpp"
#include "BackendComponents.hpp"
#include "Components.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Formatter/UserUnits.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Math/Util.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StringFormat.hpp"

#include <memory>
static double ToUser(double value, UnitGroup group) noexcept {
  return Units::ToUserUnit(value, Units::GetUserUnitByGroup(group));
}
template<std::size_t N>
static void SetUserFormat(char (&buffer)[N], UnitGroup group) noexcept {
  StringFormat(buffer, N, "%%.0f %s",
               Units::GetUnitName(Units::GetUserUnitByGroup(group)));
}
static bool CommitUser(bool &changed, double &dest, double user,
                       UnitGroup group, double step,
                       std::string_view key) noexcept {
  const Unit unit = Units::GetUserUnitByGroup(group);
  if (fabs(user - Units::ToUserUnit(dest, unit)) < step / 100.)
    return false;
  if (!ConfigPanel::CommitSetting(changed, dest, Units::ToSysUnit(user, unit)))
    return false;
  if (!key.empty())
    Profile::Set(key, dest);
  return true;
}
std::unique_ptr<Widget> CreateSafetyFactorsConfigPanel()
{
  const ComputerSettings &settings = CommonInterface::GetComputerSettings();
  const TaskBehaviour &task = settings.task;
  const double safety_mc_step = GetUserVerticalSpeedStep();
  const Unit vertical_speed =
    Units::GetUserUnitByGroup(UnitGroup::VERTICAL_SPEED);
  struct Fields {
    double arrival_height, terrain_height, degradation, safety_mc, risk_gamma;
    AbortTaskMode abort_task_mode;
    bool auto_bugs, turn_back_marker;
    char height_format[32], mc_format[32];
  };
  auto fields = std::make_shared<Fields>(Fields{
    ToUser(task.safety_height_arrival, UnitGroup::ALTITUDE),
    ToUser(task.route_planner.safety_height_terrain, UnitGroup::ALTITUDE),
    (1 - settings.polar.degradation_factor) * 100,
    ToUser(task.safety_mc, UnitGroup::VERTICAL_SPEED),
    task.risk_gamma,
    task.abort_task_mode,
    settings.polar.auto_bugs,
    task.turn_back_marker_enabled,
  });
  SetUserFormat(fields->height_format, UnitGroup::ALTITUDE);
  StringFormat(fields->mc_format, sizeof fields->mc_format, "%s %s",
               GetUserVerticalSpeedFormat(false, false),
               Units::GetUnitName(vertical_speed));
  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

  list->AddFloat(_("Arrival height"),
                 _("The height above terrain that the glider should arrive at for a safe landing."),
                 fields->height_format, "%.0f", 0, 2000, 10, false,
                 fields->arrival_height);
  list->AddFloat(_("Terrain height"),
                 _("The height above terrain that the glider must clear during final glide."),
                 fields->height_format, "%.0f", 0, 1000, 10, false,
                 fields->terrain_height);

  static constexpr StaticEnumChoice abort_task_mode_list[] = {
    { AbortTaskMode::SIMPLE, N_("Simple"),
      N_("Reachable airfields are listed first (nearest at top), then "
         "outlanding sites (nearest at top).") },
    { AbortTaskMode::TASK, N_("Task"),
      N_("Reachable airfields are listed first (smallest detour to the "
         "active turnpoint at top), then outlanding sites.") },
    { AbortTaskMode::HOME, N_("Home"),
      N_("Reachable airfields are listed first (smallest detour toward "
         "home at top), then outlanding sites.") },
    nullptr
  };

  list->AddEnum(_("Alternates mode"),
                _("Determines sorting of alternates in the alternates dialog "
                  "and in abort mode."),
                abort_task_mode_list, fields->abort_task_mode);
  list->AddFloat(_("Polar degradation"), /* xgettext:no-c-format */
                 _("A permanent polar degradation. "
                   "0% means no degradation, "
                   "50% indicates the glider's sink rate is doubled."),
                 "%.0f %%", "%.0f", 0, 50, 1, false,
                 fields->degradation, true);
  list->AddSwitch(_("Auto bugs"), /* xgettext:no-c-format */
                  _("If enabled, adds 1% to the bugs setting after each full hour while flying."),
                  fields->auto_bugs, true);
  list->AddFloat(_("Safety MC"),
                 _("The MacCready setting used, when safety MC is enabled for reach calculations, in task abort mode and for determining arrival altitude at airfields."),
                 fields->mc_format,
                 GetUserVerticalSpeedFormat(false, false),
                 0, Units::ToUserVSpeed(10), safety_mc_step, false,
                 fields->safety_mc, true);
  list->AddFloat(_("STF risk factor"),
                 _("The STF risk factor reduces the MacCready setting used to calculate speed to fly as the glider gets low, in order to compensate for risk. Set to 0.0 for no compensation, 1.0 scales MC linearly with current height (with reference to height of the maximum climb). If considered, 0.3 is recommended."),
                 "%.1f %s", "%.1f", 0, 1, 0.1, false,
                 fields->risk_gamma, true);
  list->AddSwitch(C_("Setting", "Turn back marker"),
                  _("Show a green triangle on the map along the current track "
                    "indicating the furthest point from which the active task "
                    "waypoint or Goto target can still be reached with the "
                    "current altitude and conditions. "
                    "The triangle is only shown during cruise when the target "
                    "is reachable."),
                  fields->turn_back_marker);

  list->SetSaveCallback([fields, safety_mc_step](bool &changed) {
    ComputerSettings &settings = CommonInterface::SetComputerSettings();
    TaskBehaviour &task = settings.task;

    CommitUser(changed, task.safety_height_arrival,
               fields->arrival_height, UnitGroup::ALTITUDE, 10,
               ProfileKeys::SafetyAltitudeArrival);
    CommitUser(changed, task.route_planner.safety_height_terrain,
               fields->terrain_height, UnitGroup::ALTITUDE, 10,
               ProfileKeys::SafetyAltitudeTerrain);
    ConfigPanel::CommitSetting(changed, task.abort_task_mode,
                               fields->abort_task_mode,
                               ProfileKeys::AbortTaskMode);

    const double degradation =
      (1 - settings.polar.degradation_factor) * 100;
    if (fields->degradation != degradation) {
      settings.polar.SetDegradationFactor(1 - fields->degradation / 100);
      Profile::Set(ProfileKeys::PolarDegradation,
                   settings.polar.degradation_factor);
      backend_components->SetTaskPolar(settings.polar);
      changed = true;
    }

    ConfigPanel::CommitSetting(changed, settings.polar.auto_bugs,
                               fields->auto_bugs, ProfileKeys::AutoBugs);

    if (CommitUser(changed, task.safety_mc, fields->safety_mc,
                   UnitGroup::VERTICAL_SPEED, safety_mc_step, {}))
      Profile::Set(ProfileKeys::SafetyMacCready,
                   iround(task.safety_mc * 10));

    if (ConfigPanel::CommitSetting(changed, task.risk_gamma,
                                   fields->risk_gamma))
      Profile::Set(ProfileKeys::RiskGamma,
                   iround(task.risk_gamma * 10));

    ConfigPanel::CommitSetting(changed, task.turn_back_marker_enabled,
                               fields->turn_back_marker,
                               ProfileKeys::TurnBackMarkerEnabled);
    return true;
  });

  return list;
}
