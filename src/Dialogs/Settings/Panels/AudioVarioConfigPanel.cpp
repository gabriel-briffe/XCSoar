// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AudioVarioConfigPanel.hpp"
#include "Audio/Features.hpp"
#include "Audio/VarioGlue.hpp"
#include "Audio/VarioSettings.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Float.hpp"
#include "Formatter/UserUnits.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <cmath>
#include <memory>

static constexpr StaticEnumChoice switching_modes[] = {
  { VarioSoundSwitchingMode::MANUAL, NC_("Setting", "Manual") },
  { VarioSoundSwitchingMode::AUTO, NC_("Setting", "Auto") },
  nullptr
};

static DataFieldFloat
MakeDeadband(double min_user, double max_user, double sys_value) noexcept
{
  const Unit unit = Units::GetUserUnitByGroup(UnitGroup::VERTICAL_SPEED);
  DataFieldFloat df("%.1f", "%.1f %s", min_user, max_user,
                    Units::ToUserUnit(sys_value, unit),
                    GetUserVerticalSpeedStep(), false);
  df.SetUnits(Units::GetUnitName(unit));
  df.SetFormat(GetUserVerticalSpeedFormat(false, true));
  return df;
}

static void
AddDeadband(GroupedListWidget &list, GroupedListWidget *page,
            const char *caption, const char *help,
            double min_user, double max_user, double &value) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.expert = true;
  options.value_callback =
    [min_user, max_user, &value](GroupedListWidget::ValueState &state) {
      state.text = MakeDeadband(min_user, max_user, value).GetAsDisplayString();
    };
  list.AddValue(caption, [page, caption, help, min_user, max_user, &value] {
    auto df = MakeDeadband(min_user, max_user, value);
    const double old_user = df.GetValue();
    if (!EditDataFieldDialog(caption, df, help))
      return;
    if (std::fabs(df.GetValue() - old_user) < df.GetStep() / 100)
      return;
    value = Units::ToSysUnit(df.GetValue(), Units::GetUserUnitByGroup(UnitGroup::VERTICAL_SPEED));
    page->UpdateValues();
  }, std::move(options));
}

std::unique_ptr<Widget>
CreateAudioVarioConfigPanel()
{
  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  if (!AudioVarioGlue::HaveAudioVario())
    return list;

  const auto &settings = CommonInterface::GetUISettings().sound.vario;

  struct Fields {
    bool enabled;
    int volume;
    VarioSoundSwitchingMode switching_mode;
    bool dead_band_enabled;
    int min_frequency;
    int zero_frequency;
    int max_frequency;
    double min_dead;
    double max_dead;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings.enabled,
    settings.volume,
    settings.switching_mode,
    settings.dead_band_enabled,
    static_cast<int>(settings.min_frequency),
    static_cast<int>(settings.zero_frequency),
    static_cast<int>(settings.max_frequency),
    settings.min_dead,
    settings.max_dead,
  });

  auto list_page = list.get();
  list->AddGroup(nullptr);
  list->AddSwitch(_("Audio Vario"),
                  _("Emulate the sound of an electronic vario."),
                  fields->enabled);
  list->AddInteger(_("Volume"), _("The audio vario sound volume."),
                   "%u %%", "%u", 0, 100, 1, fields->volume);
  list->AddEnum(C_("Setting", "Mode switching"),
                _("Choose whether the audio vario stays in manual mode or switches automatically between Vario in circling and STF in cruise. Manual mode starts in Vario after each restart and can be changed by external input events. In the built-in simulator, STF audio needs valid airspeed and total-energy vario input; without those, manual STF is silent and auto cruise falls back to vario."),
                switching_modes, fields->switching_mode);
  list->AddSwitch(_("Enable Deadband"),
                  _("Mute the audio output in when the current lift is in a " "certain range around zero"),
                  fields->dead_band_enabled);
  list->AddInteger(_("Min. Frequency"),
                   _("The tone frequency that is played at maximum sink rate."),
                   "%u Hz", "%u", 50, 3000, 50, fields->min_frequency, true);
  list->AddInteger(_("Zero Frequency"),
                   _("The tone frequency that is played at zero climb rate."),
                   "%u Hz", "%u", 50, 3000, 50, fields->zero_frequency, true);
  list->AddInteger(_("Max. Frequency"),
                   _("The tone frequency that is played at maximum climb rate."),
                   "%u Hz", "%u", 50, 3000, 50, fields->max_frequency, true);
  AddDeadband(*list, list_page, _("Deadband min. lift"),
              _("Below this lift threshold the vario will start to play sounds if the 'Deadband' feature is enabled."),
              Units::ToUserVSpeed(-5), 0, fields->min_dead);
  AddDeadband(*list, list_page, _("Deadband max. lift"),
              _("Above this lift threshold the vario will start to play sounds if the 'Deadband' feature is enabled."),
              0, Units::ToUserVSpeed(2), fields->max_dead);

  list->SetSaveCallback([fields](bool &changed) {
    auto &settings = CommonInterface::SetUISettings().sound.vario;

    ConfigPanel::CommitSetting(changed, settings.enabled, fields->enabled,
                               ProfileKeys::SoundAudioVario);

    if (fields->volume >= 0)
      ConfigPanel::CommitSetting(changed, settings.volume,
                                 static_cast<uint8_t>(fields->volume),
                                 ProfileKeys::SoundVolume);
    ConfigPanel::CommitSetting(changed, settings.switching_mode,
                               fields->switching_mode,
                               ProfileKeys::VarioSoundSwitchingMode);
    ConfigPanel::CommitSetting(changed, settings.dead_band_enabled,
                               fields->dead_band_enabled,
                               ProfileKeys::VarioDeadBandEnabled);
    auto hz = [&](unsigned &dest, int value, std::string_view key) {
      if (value >= 0)
        ConfigPanel::CommitSetting(changed, dest,
                                   static_cast<unsigned>(value), key);
    };
    hz(settings.min_frequency, fields->min_frequency,
       ProfileKeys::VarioMinFrequency);
    hz(settings.zero_frequency, fields->zero_frequency,
       ProfileKeys::VarioZeroFrequency);
    hz(settings.max_frequency, fields->max_frequency,
       ProfileKeys::VarioMaxFrequency);
    ConfigPanel::CommitSetting(changed, settings.min_dead, fields->min_dead,
                               ProfileKeys::VarioDeadBandMin);
    ConfigPanel::CommitSetting(changed, settings.max_dead, fields->max_dead,
                               ProfileKeys::VarioDeadBandMax);
    return true;
  });
  return list;
}
