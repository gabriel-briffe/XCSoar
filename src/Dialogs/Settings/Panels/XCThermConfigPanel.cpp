// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "XCThermConfigPanel.hpp"

#ifdef HAVE_HTTP

#include "ConfigListPanel.hpp"
#include "ActionInterface.hpp"
#include "Form/DataField/Enum.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Weather/Settings.hpp"
#include "Weather/xctherm/XCThermAPI.hpp"
#include "Weather/xctherm/XCThermCatalog.hpp"
#include "Weather/xctherm/XCThermMapOverlay.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"

static constexpr StaticEnumChoice xctherm_region_list[] = {
  { unsigned(XCTherm::Region::CH), N_("CH (Alps)"),
    N_("Covers the entire Alpine arc from Vienna to Perpignan, "
       "forecasting wave throughout the whole Alps region. "
       "ICON-CH model.") },
  { unsigned(XCTherm::Region::UK), N_("UKV Model"),
    N_("United Kingdom. UKV model.") },
  nullptr
};

/**
 * The account and the region of XC Therm, and whether its overlay
 * follows the flight.
 */
class XCThermConfigPanel final : public ConfigListPanel {
  StaticString<64> email, password;
  unsigned model;
  int opacity_percent;
  bool auto_switch;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Leave() noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
XCThermConfigPanel::LoadSettings() noexcept
{
  const auto &settings = CommonInterface::GetComputerSettings().weather;

  email = settings.xctherm.credentials.email;
  password = settings.xctherm.credentials.password;
  model = settings.xctherm.model;
  opacity_percent = int(settings.xctherm.opacity_percent);
  auto_switch = settings.xctherm.auto_switch;
}

void
XCThermConfigPanel::Fill() noexcept
{
  AddGroup();

  AddTextItem(_("XC Therm email"),
              _("Email address for your XC Therm account."),
              email);

  AddTextItem(_("XC Therm password"),
              _("Password for your XC Therm account."),
              password, true);

  AddGroup();

  AddEnumItem(_("XC Therm region"),
              _("Forecast region. Changes which model XC Therm fetches data "
                "from. Restart or re-download after changing."),
              xctherm_region_list, model);

  AddPercentItem(_("Overlay opacity"),
                 /* xgettext:no-c-format */
                 _("Sets the opacity of the XC Therm overlay on the map.  "
                   "50% is more transparent, 100% is fully opaque."),
                 50, 100, 5, opacity_percent);

  AddToggleItem(_("XC Therm Auto Layer/Time"),
                _("Automatically switch altitude layer based on GPS altitude "
                  "and forecast time based on UTC clock."),
                auto_switch);
}

bool
XCThermConfigPanel::Leave() noexcept
{
  /* the list of the Weather group says whether the account is set
     up: store it as soon as the page is left, not only when the
     configuration is closed */
  bool changed = false;
  Save(changed);
  if (changed)
    Profile::Save();

  return ConfigListPanel::Leave();
}

bool
XCThermConfigPanel::Save(bool &_changed) noexcept
{
  auto &settings = CommonInterface::SetComputerSettings().weather;

  const bool auto_switch_changed =
    Profile::Update(ProfileKeys::XCThermAutoSwitch,
                    settings.xctherm.auto_switch, auto_switch);
  const bool email_changed =
    Profile::Update(ProfileKeys::XCThermEmail,
                    settings.xctherm.credentials.email, email);
  const bool password_changed =
    Profile::Update(ProfileKeys::XCThermPassword,
                    settings.xctherm.credentials.password, password);
  const bool model_changed =
    Profile::Update(ProfileKeys::XCThermModel, settings.xctherm.model,
                    model);

  unsigned opacity = unsigned(opacity_percent);
  if (opacity < 50)
    opacity = 50;
  else if (opacity > 100)
    opacity = 100;
  const bool opacity_changed =
    Profile::Update(ProfileKeys::XCThermOpacity,
                    settings.xctherm.opacity_percent, opacity);
  if (opacity_changed) {
    XCTherm::ApplyOverlayOpacityFromSettings();
    ActionInterface::SendUIState(true);
  }

  XCThermAPI::Instance().ApplySessionSettings(settings.xctherm);

  _changed |= auto_switch_changed || email_changed || password_changed ||
    model_changed || opacity_changed;

  return true;
}

std::unique_ptr<Widget>
CreateXCThermConfigPanel()
{
  return std::make_unique<XCThermConfigPanel>();
}

#endif /* HAVE_HTTP */
