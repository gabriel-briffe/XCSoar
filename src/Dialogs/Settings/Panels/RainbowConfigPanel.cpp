// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RainbowConfigPanel.hpp"

#ifdef HAVE_HTTP

#include "Dialogs/DataField.hpp"
#include "Form/DataField/Password.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Weather/Settings.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StaticString.hxx"
#include "util/TruncateString.hpp"

#include <memory>

std::unique_ptr<Widget>
CreateRainbowConfigPanel()
{
  const auto &settings = CommonInterface::GetComputerSettings().weather;

  struct Fields {
    StaticString<128> api_key;
  };

  auto fields = std::make_shared<Fields>();
  fields->api_key = settings.rainbow.api_key;

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  auto *page = list.get();
  list->AddGroup(nullptr);

  const char *const help =
    _("API token from the Rainbow.ai developer portal.");
  list->AddValue(C_("Setting", "Rainbow API key"), help,
                 [fields](GroupedListWidget::ValueState &state) {
                   PasswordDataField df(fields->api_key.c_str());
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page, help] {
                   PasswordDataField df(fields->api_key.c_str());
                   if (!EditDataFieldDialog(C_("Setting", "Rainbow API key"),
                                            df, help))
                     return;
                   CopyTruncateString(fields->api_key.data(),
                                      fields->api_key.capacity(),
                                      df.GetValue());
                   page->UpdateValues();
                 });

  list->SetSaveCallback([fields](bool &changed) {
    auto &settings = CommonInterface::SetComputerSettings().weather;

    if (settings.rainbow.api_key != fields->api_key) {
      settings.rainbow.api_key = fields->api_key;
      Profile::Set(ProfileKeys::RainbowApiKey, settings.rainbow.api_key.c_str());
      changed = true;
    }

    return true;
  });

  return list;
}

#else

#include "Widget/Widget.hpp"

std::unique_ptr<Widget>
CreateRainbowConfigPanel()
{
  return nullptr;
}

#endif
