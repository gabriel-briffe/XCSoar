// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "LoggerConfigPanel.hpp"
#include "BackendComponents.hpp"
#include "Components.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Time.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Logger/NMEALogger.hpp"
#include "Math/Util.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "UtilsSettings.hpp"
#include "Widget/GroupedListWidget.hpp"
#include <cmath>
#include <memory>
using namespace std::chrono;

static constexpr StaticEnumChoice auto_logger_list[] = {
  { LoggerSettings::AutoLogger::ON, N_("On") },
  { LoggerSettings::AutoLogger::START_ONLY, N_("Start only") },
  { LoggerSettings::AutoLogger::OFF, N_("Off") }, nullptr
};

std::unique_ptr<Widget>
CreateLoggerConfigPanel()
{
  const LoggerSettings &logger = CommonInterface::GetComputerSettings().logger;
  const Unit mass_unit = Units::GetUserUnitByGroup(UnitGroup::MASS);
  struct Fields {
    StaticString<64> pilot_name, copilot_name;
    StaticString<32> logger_id, crew_format;
    double crew_mass;
    duration<unsigned> time_step_cruise, time_step_circling;
    LoggerSettings::AutoLogger auto_logger;
    bool enable_nmea_logger, enable_flight_logger;
  };

  auto fields = std::make_shared<Fields>();
  fields->pilot_name = logger.pilot_name.c_str();
  fields->copilot_name = logger.copilot_name.c_str();
  fields->crew_mass = Units::ToUserMass(logger.crew_mass_template);
  fields->crew_format.Format("%%.0f %s", Units::GetUnitName(mass_unit));
  fields->time_step_cruise = logger.time_step_cruise;
  fields->time_step_circling = logger.time_step_circling;
  fields->auto_logger = logger.auto_logger;
  fields->enable_nmea_logger = logger.enable_nmea_logger;
  fields->enable_flight_logger = logger.enable_flight_logger;
  fields->logger_id = logger.logger_id.c_str();
  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();

  const auto add_step = [&](const char *caption, const char *help,
                            duration<unsigned> Fields::*member) {
    GroupedListWidget::ItemOptions options;
    options.help = help;
    options.expert = true;
    options.value_callback = [=](GroupedListWidget::ValueState &state) {
      DataFieldTime df(seconds{1}, seconds{30},
                       duration_cast<seconds>((*fields).*member), seconds{1}, nullptr);
      state.text = df.GetAsDisplayString();
    };
    page->AddValue(caption, [=] {
      auto &value = (*fields).*member;
      DataFieldTime df(seconds{1}, seconds{30},
                       duration_cast<seconds>(value), seconds{1}, nullptr);
      if (!EditDataFieldDialog(caption, df, help))
        return;
      value = duration_cast<duration<unsigned>>(df.GetValue());
      page->UpdateValues();
    }, std::move(options));
  };
  list->AddGroup(nullptr);
  list->AddText(_("Pilot name"),
                _("Name of the pilot in command, recorded in the IGC flight log."),
                fields->pilot_name.data(), fields->pilot_name.capacity());
  list->AddText(_("CoPilot name"),
                _("The co-pilot name recorded in the IGC flight log."),
                fields->copilot_name.data(),
                fields->copilot_name.capacity());
  list->AddFloat(_("Crew weight default"),
                 _("Default for all weight loaded to the glider beyond the empty weight and besides "
                   "the water ballast."),
                 fields->crew_format.c_str(), "%.0f",
                 0, Units::ToUserMass(300), 5, false, fields->crew_mass);
  add_step(_("Time step cruise"),
           _("This is the time interval between logged points when not circling."),
           &Fields::time_step_cruise);
  add_step(_("Time step circling"),
           _("This is the time interval between logged points when circling."),
           &Fields::time_step_circling);
  list->AddEnum(_("Auto. logger"),
                _("Enables the automatic starting and stopping of logger on takeoff and landing "
                  "respectively. Disable when flying paragliders."),
                auto_logger_list, fields->auto_logger, true);
  list->AddSwitch(_("NMEA Logger"),
                  _("Enable the NMEA logger on startup? If this option is disabled, "
                    "the NMEA logger can still be started manually."),
                  fields->enable_nmea_logger, true);
  list->AddSwitch(_("Log book"), _("Logs each start and landing."),
                  fields->enable_flight_logger, true);
  list->AddText(_("Logger ID"),
                _("The three-letter logger ID used in the IGC filename."),
                fields->logger_id.data(), fields->logger_id.capacity(),
                true);
  list->SetSaveCallback([fields, mass_unit](bool &changed) {
    LoggerSettings &logger = CommonInterface::SetComputerSettings().logger;

    auto save_text = [&](auto &dest, const auto &src, auto key) {
      if (ConfigPanel::CommitSetting(changed, dest, src))
        Profile::Set(key, dest.c_str());
    };
    save_text(logger.pilot_name, fields->pilot_name, ProfileKeys::PilotName);
    save_text(logger.copilot_name, fields->copilot_name,
              ProfileKeys::CoPilotName);

    const double old_user =
      Units::ToUserUnit(logger.crew_mass_template, mass_unit);
    if (std::fabs(fields->crew_mass - old_user) >= 5. / 100) {
      logger.crew_mass_template = static_cast<unsigned>(
        iround(Units::ToSysUnit(fields->crew_mass, mass_unit)));
      Profile::Set(ProfileKeys::CrewWeightTemplate, logger.crew_mass_template);
      changed = true;
    }

    ConfigPanel::CommitSetting(changed, logger.time_step_cruise,
      fields->time_step_cruise, ProfileKeys::LoggerTimeStepCruise);
    ConfigPanel::CommitSetting(changed, logger.time_step_circling,
      fields->time_step_circling, ProfileKeys::LoggerTimeStepCircling);
    ConfigPanel::CommitSetting(changed, logger.auto_logger,
      fields->auto_logger, ProfileKeys::AutoLogger);
    ConfigPanel::CommitSetting(changed, logger.enable_nmea_logger,
      fields->enable_nmea_logger, ProfileKeys::EnableNMEALogger);

    if (logger.enable_nmea_logger && backend_components->nmea_logger != nullptr)
      backend_components->nmea_logger->Enable();

    /* GlueFlightLogger is created at startup, so this needs a restart. */
    if (ConfigPanel::CommitSetting(changed, logger.enable_flight_logger,
          fields->enable_flight_logger, ProfileKeys::EnableFlightLogger))
      require_restart = true;

    save_text(logger.logger_id, fields->logger_id, ProfileKeys::LoggerID);
    return true;
  });

  return list;
}
