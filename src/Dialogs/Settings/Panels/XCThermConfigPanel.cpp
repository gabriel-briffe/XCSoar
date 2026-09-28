// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "XCThermConfigPanel.hpp"

#ifdef HAVE_HTTP

#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Password.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Weather/Settings.hpp"
#include "Weather/xctherm/XCThermAPI.hpp"
#include "Weather/xctherm/XCThermCatalog.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StaticString.hxx"
#include "util/TruncateString.hpp"

#include <memory>

static constexpr StaticEnumChoice xctherm_region_list[] = {
  { unsigned(XCTherm::Region::CH), N_("CH (Alps)"),
    N_("Covers the entire Alpine arc from Vienna to Perpignan, "
       "forecasting wave throughout the whole Alps region. "
       "ICON-CH model.") },
  { unsigned(XCTherm::Region::UK), N_("UKV Model"),
    N_("United Kingdom. UKV model.") },
  nullptr
};

std::unique_ptr<Widget>
CreateXCThermConfigPanel()
{
  const auto &settings = CommonInterface::GetComputerSettings().weather;
  struct Fields {
    StaticString<64> email, password;
    unsigned model;
    bool auto_switch;
  };

  auto fields = std::make_shared<Fields>();
  fields->email = settings.xctherm.credentials.email;
  fields->password = settings.xctherm.credentials.password;
  fields->model = settings.xctherm.model;
  fields->auto_switch = settings.xctherm.auto_switch;
  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  auto *page = list.get();
  list->AddGroup(nullptr);
  list->AddText(_("XC Therm email"),
                _("Email address for your XC Therm account."),
                fields->email.data(), fields->email.capacity());
  list->AddValue(_("XC Therm password"),
                 _("Password for your XC Therm account."),
                 [fields](GroupedListWidget::ValueState &state) {
                   PasswordDataField df(fields->password.c_str());
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page] {
                   PasswordDataField df(fields->password.c_str());
                   if (!EditDataFieldDialog(_("XC Therm password"), df,
                                            _("Password for your XC Therm account.")))
                     return;
                   CopyTruncateString(fields->password.data(),
                                      fields->password.capacity(), df.GetValue());
                   page->UpdateValues();
                 });
  list->AddEnum(_("XC Therm region"),
                _("Forecast region. Changes which model XC Therm fetches data "
                  "from. Restart or re-download after changing."),
                xctherm_region_list, fields->model);
  list->AddSwitch(_("XC Therm Auto Layer/Time"),
                  _("Automatically switch altitude layer based on GPS altitude "
                    "and forecast time based on UTC clock."),
                  fields->auto_switch);
  list->SetSaveCallback([fields](bool &changed) {
    auto &xctherm = CommonInterface::SetComputerSettings().weather.xctherm;
    auto save = [&](auto &dest, const auto &value, std::string_view key) {
      if (dest == value)
        return;
      dest = value;
      Profile::Set(key, dest.c_str());
      changed = true;
    };
    ConfigPanel::CommitSetting(changed, xctherm.auto_switch,
                               fields->auto_switch,
                               ProfileKeys::XCThermAutoSwitch);
    save(xctherm.credentials.email, fields->email, ProfileKeys::XCThermEmail);
    save(xctherm.credentials.password, fields->password,
         ProfileKeys::XCThermPassword);
    ConfigPanel::CommitSetting(changed, xctherm.model, fields->model,
                               ProfileKeys::XCThermModel);
    XCThermAPI::Instance().ApplySessionSettings(xctherm);
    return true;
  });

  return list;
}

#endif /* HAVE_HTTP */
