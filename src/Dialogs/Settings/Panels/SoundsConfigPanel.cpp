// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SoundsConfigPanel.hpp"
#include "Audio/Sound.hpp"
#include "ConfigPanel.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

std::unique_ptr<Widget>
CreateSoundsConfigPanel()
{
  const auto &settings = CommonInterface::GetUISettings().sound;

  struct Fields {
    bool enabled;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings.enabled,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddSwitch(_("Sounds"),
                  _("Play alert and status sounds.  When Off, the audio vario "
                    "is also silent."),
                  fields->enabled);

  list->SetSaveCallback([fields](bool &changed) {
    auto &settings = CommonInterface::SetUISettings().sound;

    if (ConfigPanel::CommitSetting(changed, settings.enabled,
                                   fields->enabled,
                                   ProfileKeys::Sounds))
      ApplySoundSettings(settings);

    return true;
  });

  return list;
}
