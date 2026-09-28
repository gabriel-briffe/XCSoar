// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WeatherControlsConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MainWindow.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Weather/Settings.hpp"

static constexpr StaticEnumChoice controls_height_list[] = {
  { 30, "30 %" },
  { 40, "40 %" },
  { 50, "50 %" },
  { 60, "60 %" },
  { 70, "70 %" },
  { 80, "80 %" },
  { 90, "90 %" },
  { 100, "100 %" },
  nullptr
};

/** How tall the weather controls are, as a percentage of a touch row. */
class WeatherControlsConfigPanel final : public ConfigListPanel {
  unsigned controls_height_percent;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
WeatherControlsConfigPanel::LoadSettings() noexcept
{
  controls_height_percent =
    CommonInterface::GetComputerSettings().weather.controls_height_percent;
}

void
WeatherControlsConfigPanel::Fill() noexcept
{
  AddGroup();

  AddEnumItem(_("Height"),
              _("Height of the weather overlay control rows at the bottom of "
                "the map, as a percentage of the default touch/control "
                "height."),
              controls_height_list, controls_height_percent);
}

bool
WeatherControlsConfigPanel::Save(bool &_changed) noexcept
{
  auto &settings = CommonInterface::SetComputerSettings().weather;

  unsigned height = controls_height_percent;
  if (height < 30)
    height = 30;
  else if (height > 100)
    height = 100;

  if (Profile::Update(ProfileKeys::WeatherControlsHeightPercent,
                      settings.controls_height_percent, height)) {
    _changed = true;
    if (CommonInterface::main_window != nullptr)
      CommonInterface::main_window->ReinitialiseLayout();
  }

  return true;
}

std::unique_ptr<Widget>
CreateWeatherControlsConfigPanel()
{
  return std::make_unique<WeatherControlsConfigPanel>();
}
