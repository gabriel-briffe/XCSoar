// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SkySightConfigPanel.hpp"

#ifdef HAVE_HTTP

#include "ActionInterface.hpp"
#include "DataGlobals.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Password.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Weather/Settings.hpp"
#include "Weather/SkySight/Regions.hpp"
#include "Weather/SkySight/SkySightClient.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StaticString.hxx"
#include "util/TruncateString.hpp"

#include <memory>

static void
FillSkySightRegion(DataFieldEnum &df, const char *region) noexcept
{
  if (const auto skysight = DataGlobals::GetSkySight(); skysight != nullptr) {
    for (const auto &candidate : skysight->GetRegions())
      df.addEnumText(candidate.id.c_str(), gettext(candidate.name.c_str()));
    if (!df.SetValue(region))
      df.SetValue(skysight->GetRegion().data());
  } else {
    for (const auto &candidate : SKYSIGHT_REGIONS)
      df.addEnumText(candidate.id, gettext(candidate.name));
    df.SetValue(FindSkySightRegionById(region).id);
  }
}

std::unique_ptr<Widget>
CreateSkySightConfigPanel()
{
  const auto &src = CommonInterface::GetComputerSettings().weather.skysight;
  struct Fields {
    StaticString<64> email, password;
    StaticString<32> region;
    int opacity_percent;
  };
  auto fields = std::make_shared<Fields>();
  fields->email = src.email;
  fields->password = src.password;
  fields->region = src.region;
  fields->opacity_percent = static_cast<int>(src.opacity_percent);
  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  auto *page = list.get();
  list->AddGroup(nullptr);
  list->AddText(C_("Setting", "SkySight Email"),
                _("The e-mail address you use to sign in to skysight.io."),
                fields->email.data(), fields->email.capacity());
  list->AddValue(C_("Setting", "SkySight Password"),
                 _("Your SkySight password."),
                 [fields](GroupedListWidget::ValueState &state) {
                   PasswordDataField df(fields->password.c_str());
                   state.text = df.GetAsDisplayString();
                 }, [fields, page] {
                   PasswordDataField df(fields->password.c_str());
                   if (!EditDataFieldDialog(C_("Setting", "SkySight Password"), df, _("Your SkySight password.")))
                     return;
                   CopyTruncateString(fields->password.data(),
                                      fields->password.capacity(), df.GetValue());
                   page->UpdateValues();
                 });
  const char *region_help = _("Select the SkySight region used for live weather layers.");
  list->AddValue(C_("Setting", "SkySight Region"), region_help,
                 [fields](GroupedListWidget::ValueState &state) {
                   DataFieldEnum df;
                   FillSkySightRegion(df, fields->region.c_str());
                   state.text = df.GetAsDisplayString();
                 }, [fields, page, region_help] {
                   DataFieldEnum df;
                   FillSkySightRegion(df, fields->region.c_str());
                   if (!EditDataFieldDialog(C_("Setting", "SkySight Region"), df, region_help))
                     return;
                   fields->region = df.GetAsString();
                   page->UpdateValues();
                 });
  list->AddInteger(_("Overlay opacity"),
                   /* xgettext:no-c-format */
                   _("Sets the opacity of the SkySight overlay on the map.  "
                     "50% is more transparent, 100% is fully opaque."),
                   "%d %%", "%d", 50, 100, 5, fields->opacity_percent);
  list->SetSaveCallback([fields](bool &changed) {
    auto &skysight = CommonInterface::SetComputerSettings().weather.skysight;
    bool skysight_changed = false;
    auto save = [&](auto &dest, const auto &value, std::string_view key) {
      if (dest == value) return;
      dest = value;
      Profile::Set(key, dest.c_str());
      skysight_changed = true;
    };
    save(skysight.email, fields->email, ProfileKeys::SkySightEmail);
    save(skysight.password, fields->password, ProfileKeys::SkySightPassword);
    save(skysight.region, fields->region, ProfileKeys::SkySightRegion);
    unsigned opacity = static_cast<unsigned>(fields->opacity_percent);
    if (opacity < 50)
      opacity = 50;
    else if (opacity > 100)
      opacity = 100;
    if (skysight.opacity_percent != opacity) {
      skysight.opacity_percent = opacity;
      Profile::Set(ProfileKeys::SkySightOpacity, opacity);
      if (auto client = DataGlobals::GetSkySight())
        client->ApplyOverlayOpacityFromSettings();
      ActionInterface::SendUIState(true);
      skysight_changed = true;
    }
    if (skysight_changed)
      if (auto client = DataGlobals::GetSkySight())
        client->Init();
    changed |= skysight_changed;
    return true;
  });
  return list;
}
#else
std::unique_ptr<Widget>
CreateSkySightConfigPanel() { return {}; }
#endif
