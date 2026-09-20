// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "HapticsConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "Hardware/Vibrator.hpp"

class HapticsConfigPanel final : public RowFormWidget {
public:
  HapticsConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
HapticsConfigPanel::Prepare(ContainerWindow &parent,
                            const PixelRect &rc) noexcept
{
  const UISettings &settings = CommonInterface::GetUISettings();

  RowFormWidget::Prepare(parent, rc);

#ifdef HAVE_VIBRATOR
  static constexpr StaticEnumChoice haptic_feedback_list[] = {
    { UISettings::HapticFeedback::DEFAULT, N_("OS settings") },
    { UISettings::HapticFeedback::OFF, N_("Off") },
    { UISettings::HapticFeedback::ON, N_("On") },
    nullptr
  };

  AddEnum(_("Haptic feedback"),
          _("Determines if haptic feedback like vibration is used."),
          haptic_feedback_list, (unsigned)settings.haptic_feedback);
#else
  (void)settings;
  AddReadOnly(_("Haptic feedback"),
              _("Haptic feedback is not available on this device."),
              _("Unavailable"));
#endif
}

bool
HapticsConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;

#ifdef HAVE_VIBRATOR
  UISettings &settings = CommonInterface::SetUISettings();
  changed |= SaveValueEnum(0, ProfileKeys::HapticFeedback,
                           settings.haptic_feedback);
#endif

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateHapticsConfigPanel()
{
  return std::make_unique<HapticsConfigPanel>();
}
