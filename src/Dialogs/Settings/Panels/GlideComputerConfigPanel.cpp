// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideComputerConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Time.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "UtilsSettings.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <chrono>
using namespace std::chrono;
static void
AddSeconds(GroupedListWidget &list, const char *caption,
           const char *help, seconds min_v, seconds max_v,
           seconds step, int &value) noexcept
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
static void
CommitSeconds(bool &changed, Duration &dest, int value,
              std::string_view key) noexcept
{
  const auto neu = seconds{value};
  if (neu == round<seconds>(dest)) return;
  ConfigPanel::CommitSetting(changed, dest, duration_cast<Duration>(neu));
  Profile::Set(key, neu);
}
std::unique_ptr<Widget>
CreateGlideComputerConfigPanel()
{
  const auto &settings = CommonInterface::GetComputerSettings();
  struct Fields {
    TaskBehaviour::AutoMCMode auto_mc_mode;
    bool block_stf, nav_baro_altitude, external_trigger_cruise;
    AverageEffTime average_eff_time;
    bool predict_wind_drift, wave;
    int cruise_to_circling, circling_to_cruise;
  };
  const auto secs = [](FloatDuration d) {
    return static_cast<int>(round<seconds>(d).count());
  };
  auto fields = std::make_shared<Fields>(Fields{
    settings.task.auto_mc_mode,
    settings.features.block_stf_enabled,
    settings.features.nav_baro_altitude_enabled,
    settings.circling.external_trigger_cruise_enabled,
    settings.average_eff_time,
    settings.task.glide.predict_wind_drift,
    settings.wave.enabled,
    secs(settings.circling.cruise_to_circling_mode_switch_threshold),
    secs(settings.circling.circling_to_cruise_mode_switch_threshold),
  });
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  static constexpr StaticEnumChoice auto_mc_list[] = {
    { TaskBehaviour::AutoMCMode::FINALGLIDE, N_("Final glide"),
      N_("Adjusts MC for the fastest arrival. For contest sprint tasks, the MacCready is adjusted in "
          "order to cover the greatest distance in the remaining time and reach the finish height.") },
    { TaskBehaviour::AutoMCMode::CLIMBAVERAGE, N_("Trending average climb"),
      N_("Sets MC to the trending average climb rate based on all climbs.") },
    { TaskBehaviour::AutoMCMode::BOTH, N_("Both"),
      N_("Uses trending average during task, then fastest arrival when in final glide mode.") },
    nullptr
  };
  list->AddEnum(_("Auto MC mode"),
                _("This option defines which auto MacCready algorithm is used."),
                auto_mc_list, fields->auto_mc_mode);
  list->AddSwitch(_("Block speed to fly"),
                  _("If enabled, the command speed in cruise is set to the MacCready speed to fly in "
                      "no vertical air-mass movement. If disabled, the command speed in cruise is set "
                      "to the dolphin speed to fly, equivalent to the MacCready speed with vertical "
                      "air-mass movement."),
                  fields->block_stf, true);
  list->AddSwitch(_("Nav. by baro altitude"),
                  _("When enabled and if connected to a barometric altimeter, barometric altitude is "
                      "used for all navigation functions. Otherwise GPS altitude is used."),
                  fields->nav_baro_altitude, true);
  list->AddSwitch(_("Flap forces cruise"),
                  _("When Vega variometer is connected and this option is true, the positive flap "
                      "setting switches the flight mode between circling and cruise."),
                  fields->external_trigger_cruise, true);
  static constexpr StaticEnumChoice aver_eff_list[] = {
    { ae15seconds, "15 s", N_("Preferred period for paragliders.") },
    { ae30seconds, "30 s" },
    { ae60seconds, "60 s" },
    { ae90seconds, "90 s", N_("Preferred period for gliders.") },
    { ae2minutes, "2 min" },
    { ae3minutes, "3 min" },
    nullptr
  };
  list->AddEnum(_("GR average period"),
                _("Here you can decide on how many seconds of flight this calculation must be done. "
                    "Normally for gliders a good value is 90-120 seconds, and for paragliders 15 seconds."),
                aver_eff_list, fields->average_eff_time, true);
  list->AddSwitch(_("Predict wind drift"),
                  _("Account for wind drift for the predicted circling duration. This reduces the arrival height for legs with head wind."),
                  fields->predict_wind_drift, true);
  list->AddSwitch(_("Wave assistant"),
                  _("Enable detection and display of wave lift. "
                    "When enabled, wave sources are identified and shown on the map."),
                  fields->wave);
  AddSeconds(*list, _("Cruise/Circling period"),
             _("How many seconds of turning before changing from cruise to circling mode."),
             seconds{2}, seconds{30}, seconds{1},
             fields->cruise_to_circling);
  AddSeconds(*list, _("Circling/Cruise period"),
             _("How many seconds of flying straight before changing from circling to cruise mode."),
             seconds{2}, seconds{30}, seconds{1},
             fields->circling_to_cruise);
  list->SetSaveCallback([fields](bool &changed) {
    ComputerSettings &s = CommonInterface::SetComputerSettings();
    ConfigPanel::CommitSetting(changed, s.task.auto_mc_mode,
                               fields->auto_mc_mode, ProfileKeys::AutoMcMode);
    ConfigPanel::CommitSetting(changed, s.features.block_stf_enabled,
                               fields->block_stf, ProfileKeys::BlockSTF);
    ConfigPanel::CommitSetting(changed,
                               s.features.nav_baro_altitude_enabled,
                               fields->nav_baro_altitude,
                               ProfileKeys::EnableNavBaroAltitude);
    ConfigPanel::CommitSetting(changed,
                               s.circling.external_trigger_cruise_enabled,
                               fields->external_trigger_cruise,
                               ProfileKeys::EnableExternalTriggerCruise);
    if (ConfigPanel::CommitSetting(changed, s.average_eff_time,
                                   fields->average_eff_time,
                                   ProfileKeys::AverEffTime))
      require_restart = true;
    ConfigPanel::CommitSetting(changed, s.task.glide.predict_wind_drift,
                               fields->predict_wind_drift,
                               ProfileKeys::PredictWindDrift);
    ConfigPanel::CommitSetting(changed, s.wave.enabled, fields->wave,
                               ProfileKeys::WaveAssistant);
    CommitSeconds(changed,
                  s.circling.cruise_to_circling_mode_switch_threshold,
                  fields->cruise_to_circling,
                  ProfileKeys::CruiseToCirclingModeSwitchThreshold);
    CommitSeconds(changed,
                  s.circling.circling_to_cruise_mode_switch_threshold,
                  fields->circling_to_cruise,
                  ProfileKeys::CirclingToCruiseModeSwitchThreshold);
    return true;
  });
  return list;
}
