// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TaskRulesConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Time.hpp"
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

#include <chrono>
#include <type_traits>
using namespace std::chrono;
static double ToUser(double value, UnitGroup group) noexcept {
  return Units::ToUserUnit(value, Units::GetUserUnitByGroup(group));
}
template<std::size_t N>
static void SetUserFormat(char (&buffer)[N], UnitGroup group) noexcept {
  StringFormat(buffer, N, "%%.0f %s",
               Units::GetUnitName(Units::GetUserUnitByGroup(group)));
}
template<typename T>
static void
CommitUser(bool &changed, T &dest, double user, UnitGroup group,
           double step, std::string_view key) noexcept
{
  const Unit unit = Units::GetUserUnitByGroup(group);
  if (fabs(user - Units::ToUserUnit((double)dest, unit)) < step / 100.)
    return;
  const double sys = Units::ToSysUnit(user, unit);
  const T stored = std::is_integral_v<T> ? static_cast<T>(iround(sys))
                                         : static_cast<T>(sys);
  if (ConfigPanel::CommitSetting(changed, dest, stored))
    Profile::Set(key, dest);
}
static void
AddSeconds(GroupedListWidget &list, const char *caption, const char *help,
           seconds min_v, seconds max_v, seconds step, int &value) noexcept
{
  GroupedListWidget::ItemOptions o{.help = help, .expert = true};
  o.value_callback = [&](auto &state) {
    DataFieldTime df(min_v, max_v, seconds{value}, step, nullptr);
    state.text = df.GetAsDisplayString();
  };
  list.AddValue(caption, [&] {
    DataFieldTime df(min_v, max_v, seconds{value}, step, nullptr);
    if (!EditDataFieldDialog(caption, df, help)) return;
    value = static_cast<int>(df.GetValue().count());
    list.UpdateValues();
  }, o);
}
template<typename Duration>
static void CommitSeconds(bool &changed, Duration &dest, int value,
                          std::string_view key) noexcept
{
  const auto neu = seconds{value};
  if (neu == round<seconds>(dest)) return;
  ConfigPanel::CommitSetting(changed, dest, duration_cast<Duration>(neu));
  Profile::Set(key, neu);
}
std::unique_ptr<Widget> CreateTaskRulesConfigPanel()
{
  const auto &task = CommonInterface::GetComputerSettings().task;
  const auto &start = task.ordered_defaults.start_constraints;
  const auto &finish = task.ordered_defaults.finish_constraints;
  struct Fields {
    double start_max_speed, start_max_speed_margin;
    double start_max_height, start_max_height_margin, finish_min_height;
    AltitudeReference start_height_ref, finish_height_ref;
    int pev_start_wait_time, pev_start_window;
    char speed_format[32], height_format[32];
  };
  auto fields = std::make_shared<Fields>(Fields{
    ToUser(start.max_speed, UnitGroup::HORIZONTAL_SPEED),
    ToUser(task.start_margins.max_speed_margin, UnitGroup::HORIZONTAL_SPEED),
    ToUser(start.max_height, UnitGroup::ALTITUDE),
    ToUser(task.start_margins.max_height_margin, UnitGroup::ALTITUDE),
    ToUser(finish.min_height, UnitGroup::ALTITUDE),
    start.max_height_ref,
    finish.min_height_ref,
    static_cast<int>(start.pev_start_wait_time.count()),
    static_cast<int>(start.pev_start_window.count()),
  });
  SetUserFormat(fields->speed_format, UnitGroup::HORIZONTAL_SPEED);
  SetUserFormat(fields->height_format, UnitGroup::ALTITUDE);
  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddFloat(_("Start max. speed"),
                 _("Maximum speed allowed in start observation zone. Set to 0 for no limit."),
                 fields->speed_format, "%.0f", 0, 300, 5, false,
                 fields->start_max_speed, true);
  list->AddFloat(_("Start max. speed margin"),
                 _("Maximum speed above maximum start speed to tolerate. Set to 0 for no tolerance."),
                 fields->speed_format, "%.0f", 0, 300, 5, false,
                 fields->start_max_speed_margin, true);
  list->AddFloat(_("Start max. height"),
                 _("Maximum height based on start height reference (AGL or MSL) while starting the task. "
                     "Set to 0 for no limit."),
                 fields->height_format, "%.0f", 0, 10000, 50, false,
                 fields->start_max_height, true);
  list->AddFloat(_("Start max. height margin"),
                 _("Maximum height above maximum start height to tolerate. Set to 0 for no tolerance."),
                 fields->height_format, "%.0f", 0, 10000, 50, false,
                 fields->start_max_height_margin, true);
  static constexpr StaticEnumChoice altitude_reference_list[] = {
    { AltitudeReference::AGL, N_("AGL"),
      N_("Reference is the height above the task point."), },
    { AltitudeReference::MSL, N_("MSL"),
      N_("Reference is altitude above mean sea level."), },
    nullptr
  };
  list->AddEnum(_("Start height ref."),
                _("Reference used for start max height rule."),
                altitude_reference_list, fields->start_height_ref, true);
  list->AddFloat(_("Finish min. height"),
                 _("Minimum height based on finish height reference (AGL or MSL) while finishing the task. "
                     "Set to 0 for no limit."),
                 fields->height_format, "%.0f", 0, 10000, 50, false,
                 fields->finish_min_height, true);
  list->AddEnum(_("Finish height ref."),
                _("Reference used for finish min height rule."),
                altitude_reference_list, fields->finish_height_ref, true);
  AddSeconds(*list, _("PEV start wait time"),
             _("Wait time in minutes after Pilot Event and before start gate opens. "
               "0 means start opens immediately."),
             {}, minutes{30}, minutes{1}, fields->pev_start_wait_time);
  AddSeconds(*list, _("PEV start window"),
             _("Number of minutes start remains open after Pilot Event and PEV wait time."
               "0 means start will never close after it opens."),
             {}, minutes{30}, minutes{1}, fields->pev_start_window);
  list->SetSaveCallback([fields](bool &changed) {
    auto &task = CommonInterface::SetComputerSettings().task;
    auto &otb = task.ordered_defaults;
    CommitUser(changed, otb.start_constraints.max_speed,
               fields->start_max_speed, UnitGroup::HORIZONTAL_SPEED, 5,
               ProfileKeys::StartMaxSpeed);
    CommitUser(changed, task.start_margins.max_speed_margin,
               fields->start_max_speed_margin, UnitGroup::HORIZONTAL_SPEED,
               5, ProfileKeys::StartMaxSpeedMargin);
    CommitUser(changed, otb.start_constraints.max_height,
               fields->start_max_height, UnitGroup::ALTITUDE, 50,
               ProfileKeys::StartMaxHeight);
    CommitUser(changed, task.start_margins.max_height_margin,
               fields->start_max_height_margin, UnitGroup::ALTITUDE, 50,
               ProfileKeys::StartMaxHeightMargin);
    ConfigPanel::CommitSetting(changed,
                               otb.start_constraints.max_height_ref,
                               fields->start_height_ref,
                               ProfileKeys::StartHeightRef);
    CommitUser(changed, otb.finish_constraints.min_height,
               fields->finish_min_height, UnitGroup::ALTITUDE, 50,
               ProfileKeys::FinishMinHeight);
    ConfigPanel::CommitSetting(changed,
                               otb.finish_constraints.min_height_ref,
                               fields->finish_height_ref,
                               ProfileKeys::FinishHeightRef);
    CommitSeconds(changed, otb.start_constraints.pev_start_wait_time,
                  fields->pev_start_wait_time, ProfileKeys::PEVStartWaitTime);
    CommitSeconds(changed, otb.start_constraints.pev_start_window,
                  fields->pev_start_window, ProfileKeys::PEVStartWindow);
    return true;
  });
  return list;
}
