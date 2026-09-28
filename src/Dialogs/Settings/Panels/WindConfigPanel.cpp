// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WindConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Computer/Settings.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

std::unique_ptr<Widget>
CreateWindConfigPanel()
{
  const WindSettings &wind =
    CommonInterface::GetComputerSettings().wind;

  struct Fields {
    bool circling;
    bool zig_zag;
    bool external;
  };

  auto fields = std::make_shared<Fields>(Fields{
    wind.circling_wind,
    wind.zig_zag_wind,
    wind.external_wind,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddSwitch(_("Circling wind"),
                  _("Estimate the wind vector while circling. "
                    "Requires only a GPS."),
                  fields->circling);
  list->AddSwitch(_("ZigZag wind"),
                  _("Estimate the wind vector during glides. "
                    "Requires an airspeed sensor."),
                  fields->zig_zag);
  list->AddSwitch(_("External wind"),
                  _("Should XCSoar accept wind estimates from other "
                    "instruments?"),
                  fields->external);

  list->SetSaveCallback([fields](bool &changed) {
    WindSettings &settings =
      CommonInterface::SetComputerSettings().wind;

    const bool circling_changed =
      ConfigPanel::CommitSetting(changed, settings.circling_wind,
                                 fields->circling);
    const bool zig_zag_changed =
      ConfigPanel::CommitSetting(changed, settings.zig_zag_wind,
                                 fields->zig_zag);
    if (circling_changed || zig_zag_changed)
      Profile::Set(ProfileKeys::AutoWind,
                   settings.GetLegacyAutoWindMode());

    ConfigPanel::CommitSetting(changed, settings.external_wind,
                               fields->external,
                               ProfileKeys::ExternalWind);
    return true;
  });

  return list;
}
