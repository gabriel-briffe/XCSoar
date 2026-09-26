// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SoundsConfigPanel.hpp"
#include "Audio/Sound.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/RowFormWidget.hpp"

class SoundsConfigPanel final : public RowFormWidget {
public:
  SoundsConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
SoundsConfigPanel::Prepare(ContainerWindow &parent,
                           const PixelRect &rc) noexcept
{
  const auto &settings = CommonInterface::GetUISettings().sound;

  RowFormWidget::Prepare(parent, rc);

  AddBoolean(_("Sounds"),
             _("Play alert and status sounds.  When Off, the audio vario "
               "is also silent."),
             settings.enabled);
}

bool
SoundsConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  auto &settings = CommonInterface::SetUISettings().sound;

  changed |= SaveValue(0, ProfileKeys::Sounds, settings.enabled);

  if (changed)
    ApplySoundSettings(settings);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateSoundsConfigPanel()
{
  return std::make_unique<SoundsConfigPanel>();
}
