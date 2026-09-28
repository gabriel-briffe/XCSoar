// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InterfaceConfigPanel.hpp"

#include "Asset.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Dialogs/DataField.hpp"
#include "Dialogs/FilePicker.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/File.hpp"
#include "Form/DataField/Time.hpp"
#include "Hardware/Vibrator.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Language/Table.hpp"
#include "LocalPath.hpp"
#include "Profile/Profile.hpp"
#include "Repository/FileType.hpp"
#include "UIGlobals.hpp"
#include "UtilsSettings.hpp"
#include "Version.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "system/FileUtil.hpp"
#include "system/Path.hpp"
#include "util/StaticString.hxx"
#include "util/StringCompare.hxx"

#include <chrono>
#include <memory>

using namespace std::chrono;

#ifdef HAVE_NLS

#ifdef HAVE_BUILTIN_LANGUAGES

class LanguageFileVisitor final : public File::Visitor {
  DataFieldEnum &df;
public:
  explicit LanguageFileVisitor(DataFieldEnum &_df) noexcept : df(_df) {}
  void Visit([[maybe_unused]] Path path, Path filename) override {
    if (!df.Exists(filename.c_str()))
      df.addEnumText(filename.c_str());
  }
};

#endif // HAVE_BUILTIN_LANGUAGES

static void
FillLanguage(DataFieldEnum &df) noexcept
{
  df.addEnumText(_("Automatic"));
  df.addEnumText("English");

  for (const BuiltinLanguage *l = language_table; l->resource != nullptr;
       ++l) {
    StaticString<100> display_string;
    display_string.Format("%s (%s)", l->name, l->resource);
    df.addEnumText(l->resource, display_string);
  }

#ifdef HAVE_BUILTIN_LANGUAGES
  LanguageFileVisitor lfv(df);
  VisitDataFiles("*.mo", lfv);
#endif

  df.Sort(2);
}

static void
SelectLanguage(DataFieldEnum &df) noexcept
{
  auto value_buffer = Profile::GetPath(ProfileKeys::LanguageFile);
  Path value = value_buffer == nullptr ? Path("") : Path(value_buffer);
  if (value == Path("none"))
    df.SetValue(1);
  else if (!value.empty() && value != Path("auto")) {
    const Path base = value.GetBase();
    if (base != nullptr)
      df.SetValue(base.c_str());
  }
}

static void
SaveLanguage(bool &changed, const DataFieldEnum &df) noexcept
{
  /* Missing / empty LanguageFile means automatic.  Do not persist
     "auto" just because the key was absent (#1793). */
  const auto old_buffer = Profile::GetPath(ProfileKeys::LanguageFile);
  const bool old_is_auto = old_buffer == nullptr || old_buffer.empty() ||
    old_buffer == Path("auto");
  Path old_value = old_is_auto ? Path("auto") : Path(old_buffer);
  auto old_base = old_value.GetBase();
  if (old_base == nullptr)
    old_base = old_value;

  const unsigned index = df.GetValue();
  AllocatedPath buffer = nullptr;
  const char *new_value = index == 0 ? "auto" :
    index == 1 ? "none" : df.GetAsString();
  if (index > 1) {
    buffer = ContractLocalPath(Path(new_value));
    if (buffer != nullptr)
      new_value = buffer.c_str();
  }
  const char *new_base = index > 1 ? Path(new_value).GetBase().c_str()
                                   : new_value;
  if (new_base == nullptr)
    new_base = new_value;
  if (old_value != Path(new_value) && old_base != Path(new_base)) {
    Profile::Set(ProfileKeys::LanguageFile, new_value);
    LanguageChanged = changed = true;
  }
}

#endif // HAVE_NLS

static constexpr StaticEnumChoice text_input_list[] = {
  {DialogSettings::TextInputStyle::Default, N_("Default")},
  {DialogSettings::TextInputStyle::Keyboard, N_("Keyboard")},
  {DialogSettings::TextInputStyle::HighScore, N_("HighScore Style")},
  nullptr};

#ifdef HAVE_VIBRATOR
static constexpr StaticEnumChoice haptic_feedback_list[] = {
  {UISettings::HapticFeedback::DEFAULT, N_("OS settings")},
  {UISettings::HapticFeedback::OFF, N_("Off")},
  {UISettings::HapticFeedback::ON, N_("On")},
  nullptr};
#endif

static constexpr StaticEnumChoice disclaimer_accepted_list[] = {
  {0, N_("No")}, {1, N_("Yes")}, nullptr};

static void
AddExpertValue(GroupedListWidget &list, const char *caption,
               const char *help, GroupedListWidget::ValueCallback value,
               GroupedListWidget::Callback edit) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.expert = true;
  options.value_callback = std::move(value);
  list.AddValue(caption, std::move(edit), std::move(options));
}

std::unique_ptr<Widget>
CreateInterfaceConfigPanel()
{
  const UISettings &settings = CommonInterface::GetUISettings();

  struct Fields {
    std::shared_ptr<FileDataField> input_file;
    std::shared_ptr<DataFieldTime> menu_timeout;
    DialogSettings::TextInputStyle text_input;
#ifdef HAVE_VIBRATOR
    UISettings::HapticFeedback haptic_feedback;
#endif
    bool show_quick_guide;
    bool show_release_notes;
    bool disclaimer_accepted;
#ifdef HAVE_NLS
    std::shared_ptr<DataFieldEnum> language;
#endif
  };
  bool hide_quick_guide = false;
  Profile::Get(ProfileKeys::HideQuickGuideDialogOnStartup, hide_quick_guide);
  const char *last_seen_news = Profile::Get(ProfileKeys::LastSeenNewsVersion);
  const bool news_seen = last_seen_news != nullptr &&
    StringIsEqual(last_seen_news, XCSoar_Version);
  const char *disclaimer_version =
    Profile::Get(ProfileKeys::DisclaimerAcknowledgedVersion);
  const bool disclaimer_acknowledged = disclaimer_version != nullptr &&
    StringIsEqual(disclaimer_version, XCSoar_Version);
  auto fields = std::make_shared<Fields>();
  fields->input_file = std::make_shared<FileDataField>();
  fields->input_file->SetFileType(FileType::XCI);
  fields->input_file->AddNull();
  fields->input_file->ScanMultiplePatterns(
    GetFileTypePatterns(FileType::XCI));
  if (const auto path = Profile::GetPath(ProfileKeys::InputFile);
      path != nullptr)
    fields->input_file->SetValue(path);
  fields->menu_timeout = std::make_shared<DataFieldTime>(
    seconds{1}, minutes{1},
    duration_cast<seconds>(settings.menu_timeout / 2), seconds{1}, nullptr);
  fields->text_input = settings.dialog.text_input_style;
#ifdef HAVE_VIBRATOR
  fields->haptic_feedback = settings.haptic_feedback;
#endif
  fields->show_quick_guide = !hide_quick_guide;
  fields->show_release_notes = !news_seen;
  fields->disclaimer_accepted = disclaimer_acknowledged;
#ifdef HAVE_NLS
  fields->language = std::make_shared<DataFieldEnum>();
  FillLanguage(*fields->language);
  SelectLanguage(*fields->language);
  const char *const language_help =
    _("The language options selects translations for English texts to other "
      "languages. Select English for a native interface or Automatic to localise "
      "XCSoar according to the system settings.");
#endif
  const char *const events_help =
    _("The Input Events file defines the menu system and how XCSoar responds to "
      "button presses and events from external devices.");
  const char *const timeout_help =
    _("This determines how long menus will appear on screen if the user does not make any button "
      "presses or interacts with the computer.");
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();
  list->AddGroup(nullptr);
  AddExpertValue(*list, _("Events"), events_help,
    [fields](GroupedListWidget::ValueState &state) {
      state.text = fields->input_file->GetAsDisplayString();
    },
    [page, fields, events_help] {
      if (!FilePicker(_("Events"), *fields->input_file, events_help))
        return;
      page->UpdateValues();
    });
#ifdef HAVE_NLS
  list->AddValue(_("Language"), language_help,
    [fields](GroupedListWidget::ValueState &state) {
      state.text = fields->language->GetAsDisplayString();
    },
    [page, fields, language_help] {
      auto &df = *fields->language;
      const unsigned previous = df.GetValue();
      if (!ComboPicker(_("Language"), df, language_help) ||
          df.GetValue() == previous)
        return;
      page->UpdateValues();
    });
#endif
  AddExpertValue(*list, _("Menu timeout"), timeout_help,
    [fields](GroupedListWidget::ValueState &state) {
      state.text = fields->menu_timeout->GetAsDisplayString();
    },
    [page, fields, timeout_help] {
      if (!EditDataFieldDialog(_("Menu timeout"), *fields->menu_timeout,
                               timeout_help))
        return;
      page->UpdateValues();
    });
  list->AddEnum(_("Text input style"),
                _("Determines how the user is prompted for text input (filename, teamcode etc.)"),
                text_input_list, fields->text_input, true,
                [] { return HasPointer(); });
#ifdef HAVE_VIBRATOR
  list->AddEnum(_("Haptic feedback"),
                _("Determines if haptic feedback like vibration is used."),
                haptic_feedback_list, fields->haptic_feedback, true);
#endif
  list->AddSwitch(C_("Setting", "Show Quick Guide"),
                  _("If enabled, the Quick Guide is shown when XCSoar starts."),
                  fields->show_quick_guide);
  list->AddSwitch(C_("Setting", "Show release notes"),
                  _("If enabled, the What's New page is shown on the next "
                    "startup."),
                  fields->show_release_notes);
  list->AddEnum(_("Safety disclaimer accepted"),
                _("Whether the safety disclaimer has been accepted for this "
                  "version."),
                disclaimer_accepted_list, fields->disclaimer_accepted, true);
  const bool quick_at_open = fields->show_quick_guide;
  const bool news_at_open = fields->show_release_notes;
  const bool disclaimer_at_open = fields->disclaimer_accepted;
  list->SetSaveCallback([fields, quick_at_open, news_at_open,
                         disclaimer_at_open](bool &changed) {
    UISettings &settings = CommonInterface::SetUISettings();
    Path new_value = fields->input_file->GetValue();
    const auto contracted = ContractLocalPath(new_value);
    if (contracted != nullptr)
      new_value = contracted;
    const char *old_value = Profile::Get(ProfileKeys::InputFile, "");
    if (!StringIsEqual(old_value, new_value.c_str())) {
      Profile::Set(ProfileKeys::InputFile, new_value.c_str());
      require_restart = changed = true;
    }
#ifdef HAVE_NLS
    SaveLanguage(changed, *fields->language);
#endif
    ConfigPanel::CommitSetting(changed, settings.menu_timeout,
      duration_cast<duration<unsigned>>(fields->menu_timeout->GetValue()) * 2,
      ProfileKeys::MenuTimeout);
    if (HasPointer())
      ConfigPanel::CommitSetting(changed, settings.dialog.text_input_style,
        fields->text_input, ProfileKeys::AppTextInputStyle);
#ifdef HAVE_VIBRATOR
    ConfigPanel::CommitSetting(changed, settings.haptic_feedback,
      fields->haptic_feedback, ProfileKeys::HapticFeedback);
#endif
    if (fields->show_quick_guide != quick_at_open) {
      Profile::Set(ProfileKeys::HideQuickGuideDialogOnStartup,
                   !fields->show_quick_guide);
      changed = true;
    }
    if (fields->show_release_notes != news_at_open) {
      Profile::Set(ProfileKeys::LastSeenNewsVersion,
                   fields->show_release_notes ? "" : XCSoar_Version);
      changed = true;
    }
    if (fields->disclaimer_accepted != disclaimer_at_open) {
      Profile::Set(ProfileKeys::DisclaimerAcknowledgedVersion,
                   fields->disclaimer_accepted ? XCSoar_Version : "");
      changed = true;
    }
    return true;
  });
  return list;
}
