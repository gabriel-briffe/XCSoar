// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InputConfigPanel.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Form/DataField/Enum.hpp"
#include "util/StringCompare.hxx"
#include "Interface.hpp"
#include "Asset.hpp"
#include "UtilsSettings.hpp"
#include "Language/Language.hpp"
#include "UIGlobals.hpp"
#include "Repository/FileType.hpp"
#include "Version.hpp"

using namespace std::chrono;

enum ControlIndex {
  InputFile,
  MenuTimeout,
  TextInput,
  ShowQuickGuideOnStartup,
  ShowReleaseNotesOnStartup,
  DisclaimerAccepted,
};

class InputConfigPanel final : public RowFormWidget {
public:
  InputConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
InputConfigPanel::Prepare(ContainerWindow &parent,
                          const PixelRect &rc) noexcept
{
  const UISettings &settings = CommonInterface::GetUISettings();

  RowFormWidget::Prepare(parent, rc);

  AddFile(_("Events"),
          _("The Input Events file defines the menu system and how XCSoar responds to "
            "button presses and events from external devices."),
          ProfileKeys::InputFile,
          GetFileTypePatterns(FileType::XCI),
          FileType::XCI);
  SetExpertRow(InputFile);

  AddDuration(_("Menu timeout"),
              _("This determines how long menus will appear on screen if the user does not make any button "
                "presses or interacts with the computer."),
              seconds{1}, minutes{1}, seconds{1},
              settings.menu_timeout / 2);
  SetExpertRow(MenuTimeout);

  static constexpr StaticEnumChoice text_input_list[] = {
    { DialogSettings::TextInputStyle::Default, N_("Default") },
    { DialogSettings::TextInputStyle::Keyboard, N_("Keyboard") },
    { DialogSettings::TextInputStyle::HighScore,
      N_("HighScore Style") },
    nullptr
  };

  AddEnum(_("Text input style"),
          _("Determines how the user is prompted for text input (filename, teamcode etc.)"),
          text_input_list, (unsigned)settings.dialog.text_input_style);
  SetExpertRow(TextInput);

  /* on-screen keyboard doesn't work without a pointing device
     (mouse or touch screen) */
  SetRowVisible(TextInput, HasPointer());

  bool hide_quick_guide = false;
  Profile::Get(ProfileKeys::HideQuickGuideDialogOnStartup,
               hide_quick_guide);
  AddBoolean(C_("Setting", "Show Quick Guide"),
             _("If enabled, the Quick Guide is shown when XCSoar starts."),
             !hide_quick_guide);

  const char *last_seen_news =
    Profile::Get(ProfileKeys::LastSeenNewsVersion);
  const bool news_seen = last_seen_news != nullptr &&
    StringIsEqual(last_seen_news, XCSoar_Version);
  AddBoolean(C_("Setting", "Show release notes"),
             _("If enabled, the What's New page is shown on the next "
               "startup."),
             !news_seen);

  const char *disclaimer_acknowledged_version =
    Profile::Get(ProfileKeys::DisclaimerAcknowledgedVersion);
  const bool disclaimer_acknowledged =
    disclaimer_acknowledged_version != nullptr &&
    StringIsEqual(disclaimer_acknowledged_version, XCSoar_Version);

  static constexpr StaticEnumChoice disclaimer_accepted_list[] = {
    { 0, N_("No") },
    { 1, N_("Yes") },
    nullptr
  };

  AddEnum(_("Safety disclaimer accepted"),
          _("Whether the safety disclaimer has been accepted for this "
            "version."),
          disclaimer_accepted_list,
          disclaimer_acknowledged ? 1u : 0u);
  SetExpertRow(DisclaimerAccepted);
}

bool
InputConfigPanel::Save(bool &_changed) noexcept
{
  UISettings &settings = CommonInterface::SetUISettings();
  bool changed = false;

  if (SaveValueFileReader(InputFile, ProfileKeys::InputFile))
    require_restart = changed = true;

  duration<unsigned> menu_timeout = GetValueTime(MenuTimeout) * 2;
  if (settings.menu_timeout != menu_timeout) {
    settings.menu_timeout = menu_timeout;
    Profile::Set(ProfileKeys::MenuTimeout, menu_timeout);
    changed = true;
  }

  if (HasPointer())
    changed |= SaveValueEnum(TextInput, ProfileKeys::AppTextInputStyle,
                             settings.dialog.text_input_style);

  bool hide_quick_guide = false;
  Profile::Get(ProfileKeys::HideQuickGuideDialogOnStartup, hide_quick_guide);
  if (SaveValue(ShowQuickGuideOnStartup,
                ProfileKeys::HideQuickGuideDialogOnStartup,
                hide_quick_guide, true))
    changed = true;

  const bool show_release_notes = GetValueBoolean(ShowReleaseNotesOnStartup);
  const char *last_seen_news =
    Profile::Get(ProfileKeys::LastSeenNewsVersion);
  const bool news_seen = last_seen_news != nullptr &&
    StringIsEqual(last_seen_news, XCSoar_Version);
  if (show_release_notes != !news_seen) {
    if (show_release_notes)
      Profile::Set(ProfileKeys::LastSeenNewsVersion, "");
    else
      Profile::Set(ProfileKeys::LastSeenNewsVersion, XCSoar_Version);
    changed = true;
  }

  const bool disclaimer_accepted =
    GetValueEnum(DisclaimerAccepted) != 0;
  const char *disclaimer_acknowledged_version =
    Profile::Get(ProfileKeys::DisclaimerAcknowledgedVersion);
  const bool disclaimer_acknowledged =
    disclaimer_acknowledged_version != nullptr &&
    StringIsEqual(disclaimer_acknowledged_version, XCSoar_Version);
  if (disclaimer_accepted != disclaimer_acknowledged) {
    if (disclaimer_accepted)
      Profile::Set(ProfileKeys::DisclaimerAcknowledgedVersion,
                   XCSoar_Version);
    else
      Profile::Set(ProfileKeys::DisclaimerAcknowledgedVersion, "");
    changed = true;
  }

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateInputConfigPanel()
{
  return std::make_unique<InputConfigPanel>();
}
