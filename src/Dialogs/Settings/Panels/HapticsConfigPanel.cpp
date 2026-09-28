// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "HapticsConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Hardware/Vibrator.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "UISettings.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

#ifdef HAVE_VIBRATOR
static constexpr StaticEnumChoice haptic_feedback_list[] = {
  { UISettings::HapticFeedback::DEFAULT, N_("OS settings") },
  { UISettings::HapticFeedback::OFF, N_("Off") },
  { UISettings::HapticFeedback::ON, N_("On") },
  nullptr
};
#endif

std::unique_ptr<Widget>
CreateHapticsConfigPanel()
{
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

#ifdef HAVE_VIBRATOR
  const UISettings &settings = CommonInterface::GetUISettings();

  struct Fields {
    UISettings::HapticFeedback haptic_feedback;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings.haptic_feedback,
  });

  list->AddEnum(_("Haptic feedback"),
                _("Determines if haptic feedback like vibration is used."),
                haptic_feedback_list, fields->haptic_feedback);

  list->SetSaveCallback([fields](bool &changed) {
    UISettings &settings = CommonInterface::SetUISettings();
    ConfigPanel::CommitSetting(changed, settings.haptic_feedback,
                               fields->haptic_feedback,
                               ProfileKeys::HapticFeedback);
    return true;
  });
#else
  list->AddValue(_("Haptic feedback"),
                 _("Haptic feedback is not available on this device."),
                 [](GroupedListWidget::ValueState &state) {
                   state.text = _("Unavailable");
                 });
#endif

  return list;
}
