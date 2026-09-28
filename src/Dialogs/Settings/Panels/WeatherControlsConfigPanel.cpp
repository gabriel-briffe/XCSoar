// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WeatherControlsConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MainWindow.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Weather/Settings.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

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

std::unique_ptr<Widget>
CreateWeatherControlsConfigPanel()
{
  const auto &settings = CommonInterface::GetComputerSettings().weather;

  struct Fields {
    unsigned controls_height_percent;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings.controls_height_percent,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddEnum(_("Height"),
                _("Height of the weather overlay control rows at the bottom of "
                  "the map, as a percentage of the default touch/control height."),
                controls_height_list, fields->controls_height_percent);

  list->SetSaveCallback([fields](bool &changed) {
    auto &settings = CommonInterface::SetComputerSettings().weather;

    unsigned height = fields->controls_height_percent;
    if (height < 30)
      height = 30;
    else if (height > 100)
      height = 100;

    if (ConfigPanel::CommitSetting(changed, settings.controls_height_percent,
                                   height,
                                   ProfileKeys::WeatherControlsHeightPercent) &&
        CommonInterface::main_window != nullptr)
      CommonInterface::main_window->ReinitialiseLayout();

    return true;
  });

  return list;
}
