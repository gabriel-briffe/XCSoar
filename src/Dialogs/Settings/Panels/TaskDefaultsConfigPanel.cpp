// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TaskDefaultsConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Engine/Task/Factory/AbstractTaskFactory.hpp"
#include "Engine/Task/Ordered/OrderedTask.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Float.hpp"
#include "Form/DataField/Time.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Task/TypeStrings.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <chrono>
#include <cmath>
#include <vector>

using namespace std::chrono;

static const char *const Caption_GateWidth = N_("Gate width");
static const char *const Caption_Radius = N_("Radius");

static void
FillPointTypes(DataFieldEnum &df, const LegalPointSet &legal,
               TaskPointFactoryType value) noexcept
{
  df.EnableItemHelp(true);

  for (unsigned i = 0; i < legal.N; ++i) {
    const auto type = TaskPointFactoryType(i);
    if (!legal.Contains(type))
      continue;

    df.addEnumText(OrderedTaskPointName(type), (unsigned)type,
                   OrderedTaskPointDescription(type));
  }

  df.SetValue(value);
}

static void
AddPointType(GroupedListWidget &list, GroupedListWidget *page,
             const char *caption, const char *help,
             TaskPointFactoryType &value, LegalPointSet legal) noexcept
{
  list.AddValue(caption, help,
                [&value](GroupedListWidget::ValueState &state) {
                  state.text = OrderedTaskPointName(value);
                },
                [page, caption, help, &value, legal] {
                  DataFieldEnum df;
                  FillPointTypes(df, legal, value);
                  if (!EditDataFieldDialog(caption, df, help))
                    return;

                  value = TaskPointFactoryType(df.GetValue());
                  page->UpdateValues();
                });
}

static void
AddDistance(GroupedListWidget &list, GroupedListWidget *page,
            const char *caption, const char *help, double &sys_value,
            std::function<bool()> shown) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.value_callback = [&sys_value, shown](GroupedListWidget::ValueState &state) {
    const Unit unit = Units::GetUserUnitByGroup(UnitGroup::DISTANCE);
    DataFieldFloat df("%.1f", "%.1f %s", 0.1, 100,
                      Units::ToUserUnit(sys_value, unit), 1.0, true);
    df.SetUnits(Units::GetUnitName(unit));
    state.text = df.GetAsDisplayString();
    if (shown)
      state.hidden = !shown();
  };
  list.AddValue(caption, [page, caption, help, &sys_value] {
    const Unit unit = Units::GetUserUnitByGroup(UnitGroup::DISTANCE);
    const double old_user = Units::ToUserUnit(sys_value, unit);
    DataFieldFloat df("%.1f", "%.1f %s", 0.1, 100, old_user, 1.0, true);
    df.SetUnits(Units::GetUnitName(unit));
    if (!EditDataFieldDialog(caption, df, help))
      return;
    if (std::fabs(df.GetValue() - old_user) >= df.GetStep() / 100) {
      sys_value = Units::ToSysUnit(df.GetValue(), unit);
      page->UpdateValues();
    }
  }, std::move(options));
}

static void
AddDurationRow(GroupedListWidget &list, GroupedListWidget *page,
               const char *caption, const char *help,
               seconds min_value, seconds max_value, seconds step,
               duration<unsigned> &value, bool expert) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.expert = expert;
  options.value_callback = [&value, min_value, max_value, step](GroupedListWidget::ValueState &state) {
    DataFieldTime df(min_value, max_value, duration_cast<seconds>(value), step, nullptr);
    state.text = df.GetAsDisplayString();
  };
  list.AddValue(caption, [page, caption, help, min_value, max_value, step, &value] {
    DataFieldTime df(min_value, max_value, duration_cast<seconds>(value), step, nullptr);
    if (!EditDataFieldDialog(caption, df, help))
      return;
    value = duration_cast<duration<unsigned>>(df.GetValue());
    page->UpdateValues();
  }, std::move(options));
}

std::unique_ptr<Widget>
CreateTaskDefaultsConfigPanel()
{
  const TaskBehaviour &task_behaviour =
    CommonInterface::GetComputerSettings().task;
  OrderedTask temptask(task_behaviour);
  temptask.SetFactory(TaskFactoryType::RACING);
  const auto &factory = temptask.GetFactory();
  const std::vector<TaskFactoryType> factory_types =
    temptask.GetFactoryTypes();

  struct Fields {
    TaskPointFactoryType start_type;
    double start_radius;
    TaskPointFactoryType finish_type;
    double finish_radius;
    TaskPointFactoryType turnpoint_type;
    double turnpoint_radius;
    TaskFactoryType task_type;
    duration<unsigned> aat_min_time;
    duration<unsigned> optimise_targets_margin;
  };

  auto fields = std::make_shared<Fields>(Fields{
    task_behaviour.sector_defaults.start_type,
    task_behaviour.sector_defaults.start_radius,
    task_behaviour.sector_defaults.finish_type,
    task_behaviour.sector_defaults.finish_radius,
    task_behaviour.sector_defaults.turnpoint_type,
    task_behaviour.sector_defaults.turnpoint_radius,
    task_behaviour.task_type_default,
    task_behaviour.ordered_defaults.aat_min_time,
    task_behaviour.optimise_targets_margin,
  });

  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();
  list->AddGroup(nullptr);

  AddPointType(*list, page, _("Start point"),
               _("Default start type for new tasks you create."),
               fields->start_type, factory.GetValidStartTypes());
  AddDistance(*list, page, gettext(Caption_GateWidth), _("Default radius or gate width of the start zone for new tasks."), fields->start_radius, [fields] { return fields->start_type == TaskPointFactoryType::START_LINE; });
  AddDistance(*list, page, gettext(Caption_Radius), _("Default radius or gate width of the start zone for new tasks."), fields->start_radius, [fields] { return fields->start_type != TaskPointFactoryType::START_LINE; });

  list->AddGroup(nullptr);
  AddPointType(*list, page, _("Finish point"),
               _("Default finish type for new tasks you create."),
               fields->finish_type, factory.GetValidFinishTypes());
  AddDistance(*list, page, gettext(Caption_GateWidth), _("Default radius or gate width of the finish zone in new tasks."), fields->finish_radius, [fields] { return fields->finish_type == TaskPointFactoryType::FINISH_LINE; });
  AddDistance(*list, page, gettext(Caption_Radius), _("Default radius or gate width of the finish zone in new tasks."), fields->finish_radius, [fields] { return fields->finish_type != TaskPointFactoryType::FINISH_LINE; });

  list->AddGroup(nullptr);
  AddPointType(*list, page, _("Turn point"),
               _("Default turn point type for new tasks you create."),
               fields->turnpoint_type, factory.GetValidIntermediateTypes());
  AddDistance(*list, page, gettext(Caption_Radius), _("Default radius of turnpoint cylinders and sectors in new tasks."), fields->turnpoint_radius, {});

  list->AddGroup(nullptr);
  list->AddValue(_("Task"),
                 _("Default task type for new tasks you create."),
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = OrderedTaskFactoryName(fields->task_type);
                 },
                 [fields, page, factory_types] {
                   DataFieldEnum df;
                   df.EnableItemHelp(true);
                   for (const auto type : factory_types)
                     df.addEnumText(OrderedTaskFactoryName(type),
                                    (unsigned)type,
                                    OrderedTaskFactoryDescription(type));

                   df.SetValue(fields->task_type);
                   if (!EditDataFieldDialog(_("Task"), df, _("Default task type for new tasks you create.")))
                     return;
                   fields->task_type = TaskFactoryType(df.GetValue());
                   page->UpdateValues();
                 });
  AddDurationRow(*list, page, _("AAT min. time"),
                 _("Default AAT min. time for new AAT tasks."),
                 minutes{1}, hours{10}, minutes{1},
                 fields->aat_min_time, false);
  AddDurationRow(*list, page, _("Optimisation margin"), _("Safety margin for AAT task optimisation. Optimisation " "seeks to complete the task at the minimum time plus this margin time."), {}, minutes{30}, minutes{1}, fields->optimise_targets_margin, true);

  list->SetSaveCallback([fields](bool &changed) {
    auto &task = CommonInterface::SetComputerSettings().task;
    auto &sector = task.sector_defaults;
    auto commit = [&](auto &dest, const auto &value, std::string_view key) {
      ConfigPanel::CommitSetting(changed, dest, value, key);
    };
    commit(sector.start_type, fields->start_type, ProfileKeys::StartType);
    commit(sector.start_radius, fields->start_radius, ProfileKeys::StartRadius);
    commit(sector.turnpoint_type, fields->turnpoint_type, ProfileKeys::TurnpointType);
    commit(sector.turnpoint_radius, fields->turnpoint_radius, ProfileKeys::TurnpointRadius);
    commit(sector.finish_type, fields->finish_type, ProfileKeys::FinishType);
    commit(sector.finish_radius, fields->finish_radius, ProfileKeys::FinishRadius);
    commit(task.task_type_default, fields->task_type, ProfileKeys::TaskType);
    commit(task.ordered_defaults.aat_min_time, fields->aat_min_time, ProfileKeys::AATMinTime);
    commit(task.optimise_targets_margin, fields->optimise_targets_margin, ProfileKeys::AATTimeMargin);
    return true;
  });

  return list;
}
