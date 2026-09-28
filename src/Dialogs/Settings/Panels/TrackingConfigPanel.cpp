// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TrackingConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Password.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Tracking/TrackingSettings.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "net/State.hpp"
#include "util/NumberParser.hpp"
#include "util/StaticString.hxx"
#include "util/TruncateString.hpp"
#include "util/UTF8.hpp"

#include <climits>
#include <cstdlib>
#include <memory>
#if (defined HAVE_SKYLINES_TRACKING || defined HAVE_LIVETRACK24)

static constexpr StaticEnumChoice tracking_intervals[] = {
  { 1, "1 sec" },
  { 2, "2 sec" },
  { 3, "3 sec" },
  { 5, "5 sec" },
  { 10, "10 sec" },
  { 15, "15 sec" },
  { 20, "20 sec" },
  { 30, "30 sec" },
  { 45, "45 sec" },
  { 60, "1 min" },
  { 120, "2 min" },
  { 180, "3 min" },
  { 300, "5 min" },
  { 600, "10 min" },
  { 900, "15 min" },
  { 1200, "20 min" },
  { 1800, "30 min" },
  { 2400, "40 min" },
  { 3000, "50 min" },
  { 3600, "60 min" },
  nullptr,
};

static unsigned
FindClosestTrackingInterval(unsigned value) noexcept
{
  unsigned closest_value = 0;
  int closest_diff = INT_MAX;

  for (const StaticEnumChoice *p = tracking_intervals;
       p->display_string != nullptr; ++p) {
    const int diff = std::abs(static_cast<int>(value) -
                              static_cast<int>(p->id));
    if (diff < closest_diff) {
      closest_diff = diff;
      closest_value = p->id;
    }
  }

  return closest_value;
}
#endif

#ifdef HAVE_LIVETRACK24

static constexpr StaticEnumChoice server_list[] = {
  { 0, "www.livetrack24.com" },
  { 1, "test.livetrack24.com" },
  { 2, "livexc.dhv.de" },
  nullptr,
};

static constexpr StaticEnumChoice vehicle_type_list[] = {
  { LiveTrack24::Settings::VehicleType::GLIDER, N_("Glider") },
  { LiveTrack24::Settings::VehicleType::PARAGLIDER, N_("Paraglider") },
  { LiveTrack24::Settings::VehicleType::POWERED_AIRCRAFT,
    N_("Powered aircraft") },
  { LiveTrack24::Settings::VehicleType::HOT_AIR_BALLOON,
    N_("Hot-air balloon") },
  { LiveTrack24::Settings::VehicleType::HANGGLIDER_FLEX,
    N_("Hangglider (Flex/FAI1)") },
  { LiveTrack24::Settings::VehicleType::HANGGLIDER_RIGID,
    N_("Hangglider (Rigid/FAI5)") },
  nullptr,
};
#endif

static void
AddToggle(GroupedListWidget &list, GroupedListWidget *page,
          const char *caption, const char *help, bool &field,
          std::function<bool()> shown = {}) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.toggle = true;
  options.checked = field;
  options.help = help;
  if (shown) {
    options.value_callback =
      [shown](GroupedListWidget::ValueState &state) {
        state.hidden = !shown();
      };
  }

  list.AddItem(caption, [&field, page] {
    field = !field;
    page->UpdateValues();
  }, options);
}

static void
AddPassword(GroupedListWidget &list, GroupedListWidget *page,
            const char *caption, const char *help,
            char *buffer, std::size_t capacity,
            std::function<bool()> shown) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.value_callback =
    [buffer, shown](GroupedListWidget::ValueState &state) {
      PasswordDataField df(buffer);
      state.text = df.GetAsDisplayString();
      if (shown)
        state.hidden = !shown();
    };
  list.AddValue(caption, [page, caption, help, buffer, capacity] {
    PasswordDataField df(buffer);
    if (!EditDataFieldDialog(caption, df, help))
      return;

    CopyTruncateString(buffer, capacity, df.GetValue());
    page->UpdateValues();
  }, std::move(options));
}

std::unique_ptr<Widget>
CreateTrackingConfigPanel()
{
  const TrackingSettings &settings =
    CommonInterface::GetComputerSettings().tracking;

  struct Fields {
#ifdef HAVE_SKYLINES_TRACKING
    bool sl_enabled;
#ifdef HAVE_NET_STATE_ROAMING
    bool sl_roaming;
#endif
    unsigned sl_interval;
    bool sl_traffic;
    bool sl_near_traffic;
    StaticString<64> sl_key;
#endif
#ifdef HAVE_LIVETRACK24
    bool lt24_enabled;
    unsigned lt24_interval;
    LiveTrack24::Settings::VehicleType lt24_vehicle_type;
    StaticString<64> lt24_vehicle_name;
    StaticString<64> lt24_server;
    StaticString<64> lt24_username;
    StaticString<64> lt24_password;
#endif
  };

  auto fields = std::make_shared<Fields>();
#ifdef HAVE_SKYLINES_TRACKING
  fields->sl_enabled = settings.skylines.enabled;
#ifdef HAVE_NET_STATE_ROAMING
  fields->sl_roaming = settings.skylines.roaming;
#endif
  fields->sl_interval =
    FindClosestTrackingInterval(settings.skylines.interval);
  fields->sl_traffic = settings.skylines.traffic_enabled;
  fields->sl_near_traffic = settings.skylines.near_traffic_enabled;
  if (settings.skylines.key != 0)
    fields->sl_key.UnsafeFormat("%llX",
                                (unsigned long long)settings.skylines.key);
  else
    fields->sl_key.clear();
#endif
#ifdef HAVE_LIVETRACK24
  fields->lt24_enabled = settings.livetrack24.enabled;
  fields->lt24_interval =
    FindClosestTrackingInterval(settings.livetrack24.interval);
  fields->lt24_vehicle_type = settings.livetrack24.vehicleType;
  fields->lt24_vehicle_name = settings.livetrack24.vehicle_name;
  fields->lt24_server = settings.livetrack24.server;
  fields->lt24_username = settings.livetrack24.username;
  fields->lt24_password = settings.livetrack24.password;
#endif
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();

#ifdef HAVE_SKYLINES_TRACKING
  const auto sl_shown = [fields] { return fields->sl_enabled; };
  const auto sl_near_shown = [fields] {
    return fields->sl_enabled && fields->sl_traffic;
  };

  list->AddGroup(nullptr);
  AddToggle(*list, page, "SkyLines",
            _("Enable live tracking via the SkyLines server "
              "(tracking.skylines.aero)."),
            fields->sl_enabled);
#ifdef HAVE_NET_STATE_ROAMING
  list->AddSwitch(_("Roaming"),
                  _("Allow tracking when on a roaming mobile data connection."),
                  fields->sl_roaming, false, sl_shown);
#endif
  list->AddEnum(_("Tracking Interval"), nullptr, tracking_intervals,
                fields->sl_interval, false, sl_shown);
  AddToggle(*list, page, _("Track friends"),
            _("Download the position of your SkyLines friends live from "
              "the SkyLines server."),
            fields->sl_traffic, sl_shown);
  list->AddSwitch(_("Show nearby traffic"),
                  _("Download the position of nearby SkyLines users live from "
                    "the SkyLines server."),
                  fields->sl_near_traffic, false, sl_near_shown);
  list->AddText("Key",
                _("Your SkyLines tracking key. "
                  "This is used to identify your aircraft on the server."),
                fields->sl_key.data(), fields->sl_key.capacity(),
                false, sl_shown);
#endif

#ifdef HAVE_LIVETRACK24
  const auto lt_shown = [fields] { return fields->lt24_enabled; };

  list->AddGroup(nullptr);
  AddToggle(*list, page, "LiveTrack24", "", fields->lt24_enabled);
  list->AddEnum(_("Tracking Interval"), nullptr, tracking_intervals,
                fields->lt24_interval, false, lt_shown);
  list->AddEnum(_("Vehicle Type"), _("Type of vehicle used."),
                vehicle_type_list, fields->lt24_vehicle_type,
                false, lt_shown);
  list->AddText(_("Vehicle Name"), "Name of vehicle used.",
                fields->lt24_vehicle_name.data(),
                fields->lt24_vehicle_name.capacity(), false, lt_shown);
  list->AddValue(_("Server"), "",
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = fields->lt24_server.c_str();
                   state.hidden = !fields->lt24_enabled;
                 },
                 [fields, page] {
                   DataFieldEnum df;
                   df.AddChoices(server_list);
                   if (!df.SetValue(fields->lt24_server.c_str()))
                     df.SetValue(0u);
                   if (!EditDataFieldDialog(_("Server"), df, ""))
                     return;

                   fields->lt24_server = df.GetAsString();
                   page->UpdateValues();
                 });
  list->AddText(_("Username"), "",
                fields->lt24_username.data(),
                fields->lt24_username.capacity(), false, lt_shown);
  AddPassword(*list, page, _("Password"), "",
              fields->lt24_password.data(), fields->lt24_password.capacity(),
              lt_shown);
#endif

  list->SetSaveCallback([fields](bool &changed) {
    TrackingSettings &settings =
      CommonInterface::SetComputerSettings().tracking;

#ifdef HAVE_LIVETRACK24
    ConfigPanel::CommitSetting(changed, settings.livetrack24.interval,
                               fields->lt24_interval,
                               ProfileKeys::LiveTrack24TrackingInterval);
    ConfigPanel::CommitSetting(changed, settings.livetrack24.vehicleType,
                               fields->lt24_vehicle_type,
                               ProfileKeys::LiveTrack24TrackingVehicleType);
    if (settings.livetrack24.vehicle_name != fields->lt24_vehicle_name) {
      settings.livetrack24.vehicle_name = fields->lt24_vehicle_name;
      Profile::Set(ProfileKeys::LiveTrack24TrackingVehicleName,
                   settings.livetrack24.vehicle_name.c_str());
      changed = true;
    }
#endif

#ifdef HAVE_SKYLINES_TRACKING
    ConfigPanel::CommitSetting(changed, settings.skylines.enabled,
                               fields->sl_enabled,
                               ProfileKeys::SkyLinesTrackingEnabled);
#ifdef HAVE_NET_STATE_ROAMING
    ConfigPanel::CommitSetting(changed, settings.skylines.roaming,
                               fields->sl_roaming,
                               ProfileKeys::SkyLinesRoaming);
#endif
    ConfigPanel::CommitSetting(changed, settings.skylines.interval,
                               fields->sl_interval,
                               ProfileKeys::SkyLinesTrackingInterval);
    ConfigPanel::CommitSetting(changed, settings.skylines.traffic_enabled,
                               fields->sl_traffic,
                               ProfileKeys::SkyLinesTrafficEnabled);
    ConfigPanel::CommitSetting(changed,
                               settings.skylines.near_traffic_enabled,
                               fields->sl_near_traffic,
                               ProfileKeys::SkyLinesNearTrafficEnabled);

    const uint64_t key =
      ParseUint64(fields->sl_key.c_str(), nullptr, 16);
    if (key != settings.skylines.key) {
      settings.skylines.key = key;
      Profile::Set(ProfileKeys::SkyLinesTrackingKey, fields->sl_key.c_str());
      changed = true;
    }
#endif

#ifdef HAVE_LIVETRACK24
    ConfigPanel::CommitSetting(changed, settings.livetrack24.enabled,
                               fields->lt24_enabled,
                               ProfileKeys::LiveTrack24Enabled);
    if (settings.livetrack24.server != fields->lt24_server) {
      settings.livetrack24.server = fields->lt24_server;
      Profile::Set(ProfileKeys::LiveTrack24Server,
                   settings.livetrack24.server.c_str());
      changed = true;
    }
    if (settings.livetrack24.username != fields->lt24_username) {
      settings.livetrack24.username = fields->lt24_username;
      Profile::Set(ProfileKeys::LiveTrack24Username,
                   settings.livetrack24.username.c_str());
      changed = true;
    }
    if (settings.livetrack24.password != fields->lt24_password) {
      settings.livetrack24.password = fields->lt24_password;
      Profile::Set(ProfileKeys::LiveTrack24Password,
                   settings.livetrack24.password.c_str());
      changed = true;
    }
#endif

    return true;
  });

  return list;
}
