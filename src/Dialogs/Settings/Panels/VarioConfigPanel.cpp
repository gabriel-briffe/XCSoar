// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "VarioConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

std::unique_ptr<Widget>
CreateVarioConfigPanel()
{
  const VarioSettings &settings = CommonInterface::GetUISettings().vario;

  struct Fields {
    bool show_speed_to_fly;
    bool show_average;
    bool show_mc;
    bool show_bugs;
    bool show_ballast;
    bool show_gross;
    bool show_average_needle;
    bool show_thermal_average_needle;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings.show_speed_to_fly,
    settings.show_average,
    settings.show_mc,
    settings.show_bugs,
    settings.show_ballast,
    settings.show_gross,
    settings.show_average_needle,
    settings.show_thermal_average_needle,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddSwitch(_("Speed arrows"),
                  _("Whether to show speed command arrows on the vario gauge. In cruise mode, "
                      "arrows pointing up command slow down; arrows pointing down command speed up."),
                  fields->show_speed_to_fly, true);
  list->AddSwitch(_("Show average"),
                  _("Whether to show the average climb rate. In cruise mode, this switches to showing the "
                      "average netto airmass rate."),
                  fields->show_average, true);
  list->AddSwitch(_("Show MacReady"),
                  _("Whether to show the MacCready setting."),
                  fields->show_mc, true);
  list->AddSwitch(_("Show bugs"),
                  _("Whether to show the bugs percentage."),
                  fields->show_bugs, true);
  list->AddSwitch(_("Show ballast"),
                  _("Whether to show the ballast percentage."),
                  fields->show_ballast, true);
  list->AddSwitch(_("Show gross"),
                  _("Whether to show the gross climb rate."),
                  fields->show_gross, true);
  list->AddSwitch(_("Averager needle"),
                  _("If true, the vario gauge will display a hollow averager needle. During cruise, this "
                      "needle displays the average netto value. During circling, this needle displays the "
                      "average gross value."),
                  fields->show_average_needle, true);
  list->AddSwitch(_("Thermal Averager needle"),
                  _("If true, the vario gauge will display a thermal averager needle instead of the current climb-rate needle. During cruise, this "
                    "needle displays the last thermal average netto value. During circling, this needle displays the "
                    "average net value."),
                  fields->show_thermal_average_needle, true);

  list->SetSaveCallback([fields](bool &changed) {
    VarioSettings &settings = CommonInterface::SetUISettings().vario;

    ConfigPanel::CommitSetting(changed, settings.show_speed_to_fly,
                               fields->show_speed_to_fly,
                               ProfileKeys::AppGaugeVarioSpeedToFly);
    ConfigPanel::CommitSetting(changed, settings.show_average,
                               fields->show_average,
                               ProfileKeys::AppGaugeVarioAvgText);
    ConfigPanel::CommitSetting(changed, settings.show_mc,
                               fields->show_mc,
                               ProfileKeys::AppGaugeVarioMc);
    ConfigPanel::CommitSetting(changed, settings.show_bugs,
                               fields->show_bugs,
                               ProfileKeys::AppGaugeVarioBugs);
    ConfigPanel::CommitSetting(changed, settings.show_ballast,
                               fields->show_ballast,
                               ProfileKeys::AppGaugeVarioBallast);
    ConfigPanel::CommitSetting(changed, settings.show_gross,
                               fields->show_gross,
                               ProfileKeys::AppGaugeVarioGross);
    ConfigPanel::CommitSetting(changed, settings.show_average_needle,
                               fields->show_average_needle,
                               ProfileKeys::AppAveNeedle);
    ConfigPanel::CommitSetting(changed,
                               settings.show_thermal_average_needle,
                               fields->show_thermal_average_needle,
                               ProfileKeys::AppAveThermalNeedle);
    return true;
  });

  return list;
}
