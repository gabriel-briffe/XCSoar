// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PCMetConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Password.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Weather/Features.hpp"
#include "Weather/Settings.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StaticString.hxx"
#include "util/TruncateString.hpp"

#include <memory>

std::unique_ptr<Widget>
CreatePCMetConfigPanel()
{
  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());

#ifdef HAVE_PCMET
  struct Fields {
    StaticString<64> username, password;
  };
  const auto &src =
    CommonInterface::GetComputerSettings().weather.pcmet.www_credentials;
  auto fields = std::make_shared<Fields>();
  fields->username = src.username;
  fields->password = src.password;
  auto *page = list.get();
  list->AddGroup(nullptr);
  list->AddText(_("pc_met Username"), "",
                fields->username.data(), fields->username.capacity());
  list->AddValue(_("pc_met Password"), "",
                 [fields](GroupedListWidget::ValueState &state) {
                   PasswordDataField df(fields->password.c_str());
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page] {
                   PasswordDataField df(fields->password.c_str());
                   if (!EditDataFieldDialog(_("pc_met Password"), df, ""))
                     return;
                   CopyTruncateString(fields->password.data(),
                                      fields->password.capacity(),
                                      df.GetValue());
                   page->UpdateValues();
                 });

  list->SetSaveCallback([fields](bool &changed) {
    auto &credentials = CommonInterface::SetComputerSettings()
      .weather.pcmet.www_credentials;
    auto save = [&](auto &dest, const auto &value, std::string_view key) {
      if (dest == value)
        return;

      dest = value;
      Profile::Set(key, dest.c_str());
      changed = true;
    };
    save(credentials.username, fields->username, ProfileKeys::PCMetUsername);
    save(credentials.password, fields->password, ProfileKeys::PCMetPassword);
    return true;
  });
#endif

  return list;
}
