// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WeatherConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Weather/Features.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "net/http/Features.hpp"

#include <memory>

std::unique_ptr<Widget>
CreateWeatherConfigPanel()
{
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());

#ifdef HAVE_HTTP
  struct Fields {
    bool enable_tim;
  };

  const auto &settings = CommonInterface::GetComputerSettings().weather;
  auto fields = std::make_shared<Fields>(Fields{
    settings.enable_tim,
  });

  list->AddGroup(nullptr);
  list->AddSwitch(_("Thermal Information Map"),
                  _("Show thermal locations downloaded from Thermal Information Map (thermalmap.info)."),
                  fields->enable_tim);

  list->SetSaveCallback([fields](bool &changed) {
    auto &settings = CommonInterface::SetComputerSettings().weather;
    ConfigPanel::CommitSetting(changed, settings.enable_tim,
                               fields->enable_tim,
                               ProfileKeys::EnableThermalInformationMap);
    return true;
  });
#endif

  return list;
}
