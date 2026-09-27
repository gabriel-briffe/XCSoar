// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WindConfigPanel.hpp"
#include "Computer/Settings.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Look/DialogLook.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "Widget/Widget.hpp"

#include <memory>

namespace {

/**
 * Circling wind, ZigZag wind and External wind.  The same switches as
 * the wind dialogue, kept when the configuration dialogue closes.
 */
class WindConfigPanel final : public NullWidget {
  bool circling_wind;
  bool zig_zag_wind;
  bool external_wind;

  GroupedListWidget list;

public:
  WindConfigPanel() noexcept
    :circling_wind(CommonInterface::GetComputerSettings().wind.circling_wind),
     zig_zag_wind(CommonInterface::GetComputerSettings().wind.zig_zag_wind),
     external_wind(CommonInterface::GetComputerSettings().wind.external_wind),
     list(UIGlobals::GetDialogLook())
  {
    list.AddGroup(nullptr);
    AddSwitch(_("Circling wind"),
              _("Estimate the wind vector while circling. "
                "Requires only a GPS."),
              circling_wind);
    AddSwitch(_("ZigZag wind"),
              _("Estimate the wind vector during glides. "
                "Requires an airspeed sensor."),
              zig_zag_wind);
    AddSwitch(_("External wind"),
              _("Should XCSoar accept wind estimates from other "
                "instruments?"),
              external_wind);
  }

  PixelSize GetMinimumSize() const noexcept override {
    return list.GetMinimumSize();
  }

  PixelSize GetMaximumSize() const noexcept override {
    return list.GetMaximumSize();
  }

  void Initialise(ContainerWindow &parent,
                  const PixelRect &rc) noexcept override {
    list.Initialise(parent, rc);
  }

  void Prepare(ContainerWindow &parent,
               const PixelRect &rc) noexcept override {
    list.Prepare(parent, rc);
  }

  void Unprepare() noexcept override {
    list.Unprepare();
  }

  bool Save(bool &changed) noexcept override;

  bool Leave() noexcept override {
    return list.Leave();
  }

  void Show(const PixelRect &rc) noexcept override {
    list.Show(rc);
  }

  void Hide() noexcept override {
    list.Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    list.Move(rc);
  }

  bool SetFocus() noexcept override {
    return list.SetFocus();
  }

  bool HasFocus() const noexcept override {
    return list.HasFocus();
  }

  bool KeyPress(unsigned key_code) noexcept override {
    return list.KeyPress(key_code);
  }

private:
  void AddSwitch(const char *caption, const char *help,
                 bool &field) noexcept {
    list.AddItem(caption, [&field]{
      field = !field;
    }, {.toggle = true, .checked = field, .help = help});
  }
};

bool
WindConfigPanel::Save(bool &changed) noexcept
{
  WindSettings &settings = CommonInterface::SetComputerSettings().wind;

  const bool auto_changed = settings.circling_wind != circling_wind ||
    settings.zig_zag_wind != zig_zag_wind;
  if (auto_changed) {
    settings.circling_wind = circling_wind;
    settings.zig_zag_wind = zig_zag_wind;
    Profile::Set(ProfileKeys::AutoWind, settings.GetLegacyAutoWindMode());
    changed = true;
  }

  if (settings.external_wind != external_wind) {
    settings.external_wind = external_wind;
    Profile::Set(ProfileKeys::ExternalWind, settings.external_wind);
    changed = true;
  }

  return true;
}

} // namespace

std::unique_ptr<Widget>
CreateWindConfigPanel()
{
  return std::make_unique<WindConfigPanel>();
}
