// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SoundsConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Audio/Sound.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"

/**
 * Whether alert and status sounds play.  Off also silences the audio
 * vario.
 */
class SoundsConfigPanel final : public ConfigListPanel {
  bool enabled;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
SoundsConfigPanel::LoadSettings() noexcept
{
  enabled = CommonInterface::GetUISettings().sound.enabled;
}

void
SoundsConfigPanel::Fill() noexcept
{
  AddGroup();

  AddToggleItem(_("Sounds"),
                _("Play alert and status sounds.  When Off, the audio vario "
                  "is also silent."),
                enabled);
}

bool
SoundsConfigPanel::Save(bool &_changed) noexcept
{
  auto &settings = CommonInterface::SetUISettings().sound;

  if (Profile::Update(ProfileKeys::Sounds, settings.enabled, enabled)) {
    _changed = true;
    ApplySoundSettings(settings);
  }

  return true;
}

std::unique_ptr<Widget>
CreateSoundsConfigPanel()
{
  return std::make_unique<SoundsConfigPanel>();
}
