// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "CloudConfigPanel.hpp"
#include "Components.hpp"
#include "ConfigPanel.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "NetComponents.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Tracking/CloudSettings.hpp"
#include "Tracking/SkyLines/Key.hpp"
#include "Tracking/TrackingGlue.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "net/State.hpp"
#include "util/StringStrip.hxx"
#include "util/TriState.hpp"

#include <fmt/format.h>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

static void
AddToggle(GroupedListWidget &list, GroupedListWidget *page,
          const char *caption, const char *help, bool &field) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.toggle = true;
  options.checked = field;
  options.help = help;
  list.AddItem(caption, [&field, page] {
    field = !field;
    page->UpdateValues();
  }, options);
}

std::unique_ptr<Widget>
CreateCloudConfigPanel()
{
  const auto &settings =
    CommonInterface::GetComputerSettings().tracking.cloud;

  struct Fields {
    bool enabled;
    bool show_traffic;
#ifdef HAVE_NET_STATE_ROAMING
    bool roaming;
#endif
    bool show_thermals;
    StaticString<64> host;
    int port;
    char own_flarm[CloudSettings::OWN_FLARM_IDS_TEXT_SIZE];
  };

  auto fields = std::make_shared<Fields>();
  fields->enabled = settings.enabled == TriState::TRUE;
  fields->show_traffic = settings.show_traffic;
#ifdef HAVE_NET_STATE_ROAMING
  fields->roaming = settings.roaming;
#endif
  fields->show_thermals = settings.show_thermals;
  fields->host = settings.host;
  fields->port = static_cast<int>(settings.port);
  CloudSettings::FormatOwnFlarmIds(settings.own_flarm_ids,
                                   fields->own_flarm, sizeof(fields->own_flarm));

  const std::string own_flarm_help = fmt::format(
    fmt::runtime(
      _("Comma-separated hex FLARM / ICAO addresses (up to {}) "
        "used to hide your own aircraft from OGN traffic. Use "
        "this when the FLARM radio id is not available (for "
        "example behind an LX passthrough), or to hide "
        "additional own aircraft. Leave empty to use the "
        "device id when known.")),
    CloudSettings::MAX_OWN_FLARM_IDS);

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();
  const auto shown = [fields] { return fields->enabled; };

  list->AddGroup(nullptr);
  AddToggle(*list, page, "XCSoar Cloud",
            _("Participate in the XCSoar Cloud? This transmits your "
              "position while flying and allows receiving traffic from "
              "other XCSoar Cloud participants and OGN, as well as "
              "thermal and wave locations from the cloud server."),
            fields->enabled);
  list->AddSwitch(C_("Setting", "Show traffic"),
                  _("Receive traffic from the XCSoar Cloud server and OGN. "
                    "Requires flying with a real GPS fix."),
                  fields->show_traffic, false, shown);
#ifdef HAVE_NET_STATE_ROAMING
  list->AddSwitch(_("Roaming"),
                  _("Allow XCSoar Cloud communication when on a roaming "
                    "mobile data connection."),
                  fields->roaming, false, shown);
#endif
  list->AddSwitch(_("Show thermals"),
                  _("Obtain and show thermal locations reported by others."),
                  fields->show_thermals, false, shown);
  list->AddText(_("Server"),
                _("Hostname or IP address of the XCSoar Cloud server."),
                fields->host.data(), fields->host.capacity(),
                true, shown);
  list->AddInteger(_("Port"),
                   _("UDP port of the XCSoar Cloud server."),
                   "%u", "%u", 1, 65535, 1, fields->port,
                   true, shown);
  list->AddText(C_("Setting", "Own FLARM IDs"), own_flarm_help.c_str(),
                fields->own_flarm, sizeof(fields->own_flarm),
                true, shown);

  list->SetSaveCallback([fields](bool &changed) {
    auto &settings = CommonInterface::SetComputerSettings().tracking.cloud;

    const bool was_enabled = settings.enabled == TriState::TRUE;
    if (was_enabled != fields->enabled) {
      settings.enabled = fields->enabled
        ? TriState::TRUE
        : TriState::FALSE;
      Profile::Set(ProfileKeys::CloudEnabled, fields->enabled);

      if (settings.enabled == TriState::TRUE && settings.key == 0) {
        settings.key = SkyLinesTracking::GenerateKey();

        char s[64];
        snprintf(s, sizeof(s), "%llx",
                 (unsigned long long)settings.key);
        Profile::Set(ProfileKeys::CloudKey, s);
      }

      changed = true;
    }

    ConfigPanel::CommitSetting(changed, settings.show_traffic,
                               fields->show_traffic,
                               ProfileKeys::CloudShowTraffic);
#ifdef HAVE_NET_STATE_ROAMING
    ConfigPanel::CommitSetting(changed, settings.roaming, fields->roaming,
                               ProfileKeys::CloudRoaming);
#endif
    ConfigPanel::CommitSetting(changed, settings.show_thermals,
                               fields->show_thermals,
                               ProfileKeys::CloudShowThermals);

    if (settings.host != fields->host) {
      settings.host = fields->host;
      Profile::Set(ProfileKeys::CloudHost, settings.host.c_str());
      if (settings.host.empty())
        settings.host = CloudSettings::DEFAULT_HOST;
      changed = true;
    }

    if (fields->port >= 0) {
      const auto port = static_cast<unsigned>(fields->port);
      if (ConfigPanel::CommitSetting(changed, settings.port, port,
                                     ProfileKeys::CloudPort)) {
        if (settings.port == 0 || settings.port > 65535u) {
          settings.port = CloudSettings::DEFAULT_PORT;
          Profile::Set(ProfileKeys::CloudPort, settings.port);
        }
      }
    }

    StaticString<CloudSettings::OWN_FLARM_IDS_TEXT_SIZE> own_flarm_text;
    own_flarm_text = fields->own_flarm;
    const auto ids = CloudSettings::ParseOwnFlarmIds(own_flarm_text.c_str());
    const bool input_blank =
      Strip(std::string_view{own_flarm_text.c_str()}).empty();

    /* Non-empty garbage must not wipe a previously valid list. */
    if (input_blank || !ids.empty()) {
      bool same = settings.own_flarm_ids.size() == ids.size();
      for (unsigned i = 0; same && i < ids.size(); ++i)
        same = settings.own_flarm_ids[i] == ids[i];

      if (!same) {
        settings.own_flarm_ids = ids;
        char tmp[CloudSettings::OWN_FLARM_IDS_TEXT_SIZE];
        CloudSettings::FormatOwnFlarmIds(ids, tmp, sizeof(tmp));
        Profile::Set(ProfileKeys::CloudOwnFlarmId, tmp);
        changed = true;
      }
    }

#ifdef HAVE_TRACKING
    if (changed && net_components != nullptr &&
        net_components->tracking != nullptr)
      net_components->tracking->SetSettings(
        CommonInterface::GetComputerSettings().tracking);
#endif

    return true;
  });

  return list;
}
