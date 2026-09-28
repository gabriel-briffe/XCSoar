// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AudioConfigPanel.hpp"

#include "Audio/Features.hpp"

#ifdef HAVE_VOLUME_CONTROLLER

#include "Audio/VolumeController.hpp"
#include "ConfigPanel.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

std::unique_ptr<Widget>
CreateAudioConfigPanel()
{
  struct Fields
  {
    int master_volume;
  };

  const auto &settings = CommonInterface::GetUISettings().sound;
  auto fields          = std::make_shared<Fields>(Fields{
    settings.master_volume,
  });

  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddInteger(_("Master Volume"),
                   _("The overall audio output volume."),
                   "%d %%",
                   "%d",
                   0,
                   VolumeController::GetMaxValue(),
                   1,
                   fields->master_volume);

  list->SetSaveCallback([fields](bool &changed) {
    auto &settings = CommonInterface::SetUISettings().sound;
    const decltype(settings.master_volume) volume = fields->master_volume;
    if (ConfigPanel::CommitSetting(changed, settings.master_volume, volume))
      Profile::Set(ProfileKeys::MasterAudioVolume,
                   static_cast<unsigned>(volume));
    return true;
  });

  return list;
}

#endif /* HAVE_VOLUME_CONTROLLER */
