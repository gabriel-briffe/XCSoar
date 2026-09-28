// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InputConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Asset.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/File.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "LocalPath.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Repository/FileType.hpp"
#include "UtilsSettings.hpp"
#include "Version.hpp"
#include "system/Path.hpp"
#include "util/StringCompare.hxx"

#include <chrono>

using namespace std::chrono;

static constexpr StaticEnumChoice text_input_list[] = {
  {DialogSettings::TextInputStyle::Default, N_("Default")},
  {DialogSettings::TextInputStyle::Keyboard, N_("Keyboard")},
  {DialogSettings::TextInputStyle::HighScore, N_("HighScore Style")},
  nullptr};

static constexpr StaticEnumChoice disclaimer_accepted_list[] = {
  {0, N_("No")}, {1, N_("Yes")}, nullptr};

/**
 * How the user gives input: the event file, the menus, and the
 * dialogs which appear at startup.
 */
class InputConfigPanel final : public ConfigListPanel {
  FileDataField input_file;
  seconds menu_timeout;
  DialogSettings::TextInputStyle text_input;
  bool show_quick_guide;
  bool show_release_notes;
  bool disclaimer_accepted;

  /** the three startup switches, as they were when the page opened */
  bool quick_at_open, news_at_open, disclaimer_at_open;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
InputConfigPanel::LoadSettings() noexcept
{
  const UISettings &settings = CommonInterface::GetUISettings();

  input_file.SetFileType(FileType::XCI);
  input_file.AddNull();
  input_file.ScanMultiplePatterns(GetFileTypePatterns(FileType::XCI));
  if (const auto path = Profile::GetPath(ProfileKeys::InputFile);
      path != nullptr)
    input_file.SetValue(path);

  menu_timeout = duration_cast<seconds>(settings.menu_timeout / 2);

  text_input = settings.dialog.text_input_style;

  bool hide_quick_guide = false;
  Profile::Get(ProfileKeys::HideQuickGuideDialogOnStartup, hide_quick_guide);
  show_quick_guide = !hide_quick_guide;

  const char *last_seen_news = Profile::Get(ProfileKeys::LastSeenNewsVersion);
  const bool news_seen = last_seen_news != nullptr &&
    StringIsEqual(last_seen_news, XCSoar_Version);
  show_release_notes = !news_seen;

  const char *disclaimer_version =
    Profile::Get(ProfileKeys::DisclaimerAcknowledgedVersion);
  disclaimer_accepted = disclaimer_version != nullptr &&
    StringIsEqual(disclaimer_version, XCSoar_Version);

  quick_at_open = show_quick_guide;
  news_at_open = show_release_notes;
  disclaimer_at_open = disclaimer_accepted;
}

void
InputConfigPanel::Fill() noexcept
{
  AddGroup();

  if (IsExpert()) {
    const char *const events_help =
      _("The Input Events file defines the menu system and how XCSoar "
        "responds to button presses and events from external devices.");

    ItemOptions options{.value_size = TextSize::SMALL,
                        .value_all_lines = true,
                        .chevron = true,
                        .help = events_help};
    const char *name = input_file.GetAsDisplayString();
    if (name != nullptr && name[0] != '\0')
      options.value = name;
    else
      options.badge = C_("Badge", "none");

    AddItem(_("Events"), [this, events_help](){
      PickFile(_("Events"), events_help, input_file);
      Refresh();
    }, options);

    AddDurationItem(_("Menu timeout"),
                    _("This determines how long menus will appear on screen "
                      "if the user does not make any button presses or "
                      "interacts with the computer."),
                    1, 60, 1, menu_timeout);

    if (HasPointer())
      AddEnumItem(_("Text input style"),
                  _("Determines how the user is prompted for text input "
                    "(filename, teamcode etc.)"),
                  text_input_list, text_input);

    AddEnumItem(_("Safety disclaimer accepted"),
                _("Whether the safety disclaimer has been accepted for this "
                  "version."),
                disclaimer_accepted_list, disclaimer_accepted);
  }

  AddToggleItem(C_("Setting", "Show Quick Guide"),
                _("If enabled, the Quick Guide is shown when XCSoar starts."),
                show_quick_guide);
  AddToggleItem(C_("Setting", "Show release notes"),
                _("If enabled, the What's New page is shown on the next "
                  "startup."),
                show_release_notes);
}

bool
InputConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  UISettings &settings = CommonInterface::SetUISettings();

  Path new_value = input_file.GetValue();
  const auto contracted = ContractLocalPath(new_value);
  if (contracted != nullptr)
    new_value = contracted;

  const char *old_value = Profile::Get(ProfileKeys::InputFile, "");
  if (!StringIsEqual(old_value, new_value.c_str())) {
    Profile::Set(ProfileKeys::InputFile, new_value.c_str());
    require_restart = changed = true;
  }

  const auto timeout =
    duration_cast<duration<unsigned>>(menu_timeout) * 2;
  changed |= Profile::Update(ProfileKeys::MenuTimeout,
                             settings.menu_timeout, timeout);

  if (HasPointer())
    changed |= Profile::Update(ProfileKeys::AppTextInputStyle,
                               settings.dialog.text_input_style, text_input);

  if (show_quick_guide != quick_at_open) {
    Profile::Set(ProfileKeys::HideQuickGuideDialogOnStartup,
                 !show_quick_guide);
    changed = true;
  }

  if (show_release_notes != news_at_open) {
    Profile::Set(ProfileKeys::LastSeenNewsVersion,
                 show_release_notes ? "" : XCSoar_Version);
    changed = true;
  }

  if (disclaimer_accepted != disclaimer_at_open) {
    Profile::Set(ProfileKeys::DisclaimerAcknowledgedVersion,
                 disclaimer_accepted ? XCSoar_Version : "");
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
