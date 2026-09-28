// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "HapticsConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Hardware/Vibrator.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UISettings.hpp"

#ifdef HAVE_VIBRATOR
static constexpr StaticEnumChoice haptic_feedback_list[] = {
  { UISettings::HapticFeedback::DEFAULT, N_("OS settings") },
  { UISettings::HapticFeedback::OFF, N_("Off") },
  { UISettings::HapticFeedback::ON, N_("On") },
  nullptr
};
#endif

/** Whether a tap vibrates. */
class HapticsConfigPanel final : public ConfigListPanel {
#ifdef HAVE_VIBRATOR
  UISettings::HapticFeedback haptic_feedback;
#endif

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
HapticsConfigPanel::LoadSettings() noexcept
{
#ifdef HAVE_VIBRATOR
  haptic_feedback = CommonInterface::GetUISettings().haptic_feedback;
#endif
}

void
HapticsConfigPanel::Fill() noexcept
{
  AddGroup();

#ifdef HAVE_VIBRATOR
  AddEnumItem(_("Haptic feedback"),
              _("Determines if haptic feedback like vibration is used."),
              haptic_feedback_list, haptic_feedback);
#else
  AddItem(_("Haptic feedback"),
          {.value = _("Unavailable"),
           .help = _("Haptic feedback is not available on this device."),
           .disabled = true,
           .selectable_when_disabled = true});
#endif
}

bool
HapticsConfigPanel::Save(bool &_changed) noexcept
{
#ifdef HAVE_VIBRATOR
  UISettings &settings = CommonInterface::SetUISettings();
  _changed |= Profile::Update(ProfileKeys::HapticFeedback,
                              settings.haptic_feedback, haptic_feedback);
#else
  (void)_changed;
#endif

  return true;
}

std::unique_ptr<Widget>
CreateHapticsConfigPanel()
{
  return std::make_unique<HapticsConfigPanel>();
}
