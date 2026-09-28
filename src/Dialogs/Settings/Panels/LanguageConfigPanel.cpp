// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "LanguageConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Language/Language.hpp"
#include "Language/Table.hpp"
#include "LocalPath.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UtilsSettings.hpp"
#include "system/FileUtil.hpp"
#include "system/Path.hpp"
#include "util/StaticString.hxx"

#include <vector>

#ifdef HAVE_BUILTIN_LANGUAGES

class LanguageFileVisitor: public File::Visitor
{
private:
  DataFieldEnum &df;

public:
  LanguageFileVisitor(DataFieldEnum &_df): df(_df) {}

  void Visit([[maybe_unused]] Path path, Path filename) override {
    if (!df.Exists(filename.c_str()))
      df.addEnumText(filename.c_str());
  }
};

#endif // HAVE_BUILTIN_LANGUAGES

#ifdef HAVE_NLS

static void
FillLanguageChoices(DataFieldEnum &df) noexcept
{
  df.addEnumText(_("Automatic"));
  df.addEnumText("English");

  for (const BuiltinLanguage *l = language_table;
       l->resource != nullptr; ++l) {
    StaticString<100> display_string;
    display_string.Format("%s (%s)", l->name, l->resource);
    df.addEnumText(l->resource, display_string);
  }

#ifdef HAVE_BUILTIN_LANGUAGES
  LanguageFileVisitor lfv(df);
  VisitDataFiles("*.mo", lfv);
#endif

  df.Sort(2);

  auto value_buffer = Profile::GetPath(ProfileKeys::LanguageFile);
  Path value = value_buffer;
  if (value == nullptr)
    value = Path("");

  if (value == Path("none"))
    df.SetValue(1);
  else if (!value.empty() && value != Path("auto")) {
    const Path base = value.GetBase();
    if (base != nullptr)
      df.SetValue(base.c_str());
  }
}

/** Which translation XCSoar uses. */
class LanguageConfigPanel final : public ConfigListPanel {
  unsigned language_index = 0;
  StaticString<64> language_string;
  StaticString<100> language_display;

  void ApplySelection(DataFieldEnum &df) const noexcept;
  void PickLanguage() noexcept;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
LanguageConfigPanel::ApplySelection(DataFieldEnum &df) const noexcept
{
  /* FillLanguageChoices() selects the profile.  Put the user's
     current pick back. */
  if (!language_string.empty())
    df.SetValue(language_string.c_str());
  else
    df.SetValue(language_index);
}

void
LanguageConfigPanel::PickLanguage() noexcept
{
  const char *const help =
    _("The language options selects translations for English texts to "
      "other languages. Select English for a native interface or "
      "Automatic to localise XCSoar according to the system settings.");

  DataFieldEnum df;
  FillLanguageChoices(df);
  ApplySelection(df);

  std::vector<StaticString<100>> captions(df.Count());
  std::vector<PickerChoice> choices;
  choices.reserve(df.Count());
  int current = 0;

  for (std::size_t i = 0; i < df.Count(); ++i) {
    captions[i] = df[i].GetDisplayString();
    choices.push_back({captions[i].c_str()});
    if (df[i].GetId() == df.GetValue())
      current = int(i);
  }

  const int picked = PickChoice(_("Language"), help, choices, current);
  if (picked < 0)
    return;

  language_index = df[picked].GetId();
  language_string = df[picked].GetString();
  language_display = df[picked].GetDisplayString();
  Refresh();
}

void
LanguageConfigPanel::LoadSettings() noexcept
{
  DataFieldEnum df;
  FillLanguageChoices(df);
  language_index = df.GetValue();
  language_string = df.GetAsString();
  language_display = df.GetAsDisplayString();
}

void
LanguageConfigPanel::Fill() noexcept
{
  AddGroup();

  const char *const help =
    _("The language options selects translations for English texts to "
      "other languages. Select English for a native interface or "
      "Automatic to localise XCSoar according to the system settings.");

  AddItem(_("Language"), [this](){ PickLanguage(); },
          {.value = language_display.c_str(), .chevron = true, .help = help});
}

bool
LanguageConfigPanel::Save(bool &_changed) noexcept
{
  DataFieldEnum df;
  FillLanguageChoices(df);
  ApplySelection(df);

  const auto old_value_buffer = Profile::GetPath(ProfileKeys::LanguageFile);
  const bool old_is_auto =
    old_value_buffer == nullptr || old_value_buffer.empty() ||
    old_value_buffer == Path("auto");
  Path old_value = old_is_auto ? Path("auto") : Path(old_value_buffer);

  auto old_base = old_value.GetBase();
  if (old_base == nullptr)
    old_base = old_value;

  AllocatedPath buffer = nullptr;
  const char *new_value, *new_base;

  switch (df.GetValue()) {
  case 0:
    new_value = new_base = "auto";
    break;

  case 1:
    new_value = new_base = "none";
    break;

  default:
    new_value = df.GetAsString();
    buffer = ContractLocalPath(Path(new_value));
    if (buffer != nullptr)
      new_value = buffer.c_str();
    new_base = Path(new_value).GetBase().c_str();
    if (new_base == nullptr)
      new_base = new_value;
    break;
  }

  if (old_value != Path(new_value) &&
      old_base != Path(new_base)) {
    Profile::Set(ProfileKeys::LanguageFile, new_value);
    LanguageChanged = _changed = true;
  }

  return true;
}

#endif // HAVE_NLS

std::unique_ptr<Widget>
CreateLanguageConfigPanel()
{
#ifdef HAVE_NLS
  return std::make_unique<LanguageConfigPanel>();
#else
  class EnglishLanguagePanel final : public ConfigListPanel {
  protected:
    void LoadSettings() noexcept override {}

    void Fill() noexcept override {
      AddGroup();
      AddItem(_("Language"),
              {.value = "English",
               .disabled = true,
               .selectable_when_disabled = true});
    }

  public:
    bool Save(bool &changed) noexcept override {
      (void)changed;
      return true;
    }
  };

  return std::make_unique<EnglishLanguagePanel>();
#endif
}
