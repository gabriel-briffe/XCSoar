// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "LanguageConfigPanel.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Language/Table.hpp"
#include "LocalPath.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "UtilsSettings.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "system/FileUtil.hpp"
#include "system/Path.hpp"
#include "util/StaticString.hxx"

#include <memory>

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

#endif

std::unique_ptr<Widget>
CreateLanguageConfigPanel()
{
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

#ifdef HAVE_NLS
  struct Fields {
    unsigned language_index;
    StaticString<64> language_string;
  };

  auto fields = std::make_shared<Fields>();
  {
    DataFieldEnum df;
    FillLanguageChoices(df);
    fields->language_index = df.GetValue();
    fields->language_string = df.GetAsString();
  }

  auto *page = list.get();
  const char *const help =
    _("The language options selects translations for English texts to other "
      "languages. Select English for a native interface or Automatic to localise "
      "XCSoar according to the system settings.");

  list->AddValue(_("Language"), help,
                 [fields](GroupedListWidget::ValueState &state) {
                   DataFieldEnum df;
                   FillLanguageChoices(df);
                   /* Restore the user's current pick (Fill resets to profile). */
                   if (!fields->language_string.empty())
                     df.SetValue(fields->language_string.c_str());
                   else
                     df.SetValue(fields->language_index);
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page, help] {
                   DataFieldEnum df;
                   FillLanguageChoices(df);
                   if (!fields->language_string.empty())
                     df.SetValue(fields->language_string.c_str());
                   else
                     df.SetValue(fields->language_index);
                   if (!ComboPicker(_("Language"), df, help))
                     return;
                   fields->language_index = df.GetValue();
                   fields->language_string = df.GetAsString();
                   page->UpdateValues();
                 });

  list->SetSaveCallback([fields](bool &changed) {
    DataFieldEnum df;
    FillLanguageChoices(df);
    if (!fields->language_string.empty())
      df.SetValue(fields->language_string.c_str());
    else
      df.SetValue(fields->language_index);

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
      LanguageChanged = changed = true;
    }

    return true;
  });
#else
  list->AddValue(_("Language"), nullptr,
                 [](GroupedListWidget::ValueState &state) {
                   state.text = "English";
                 });
#endif

  return list;
}
