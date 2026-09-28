// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "NOTAMConfigPanel.hpp"
#include "Airspace/AirspaceComputerSettings.hpp"
#include "Airspace/AirspaceGlue.hpp"
#include "Components.hpp"
#include "ConfigPanel.hpp"
#include "DataComponents.hpp"
#include "Dialogs/Airspace/NOTAMList.hpp"
#include "Dialogs/Message.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "LogFile.hpp"
#include "Message.hpp"
#include "NOTAM/Config.hpp"
#include "NOTAM/Filter.hpp"
#include "NOTAM/NOTAMGlue.hpp"
#include "NetComponents.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Protection.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "net/http/Features.hpp"
#include "ui/event/Notify.hpp"
#include "util/Macros.hpp"
#include "util/StringFormat.hpp"
#include "util/UTF8.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>

#ifdef HAVE_HTTP

struct NotamFields {
  bool enabled;
  StaticString<128> api_url;
  double radius_user;
  int refresh_min;
  bool show_ifr;
  bool show_only_effective;
  double max_radius_user;
  StaticString<256> hidden_qcodes;
  char distance_format[32];
  double search_min = 0;
  double search_max = 0;
  double search_step = 0;
  bool loading = false;
  bool saving_for_manual = false;
};

enum class NotamCount {
  IFR,
  TIME,
  RADIUS,
  QCODE,
};

static unsigned
FilteredCount(NotamCount kind)
{
  NOTAMFilter::FilterStats stats{};
  if (net_components != nullptr && net_components->notam != nullptr)
    stats = net_components->notam->GetFilterStats();

  switch (kind) {
  case NotamCount::IFR:
    return stats.filtered_by_ifr;
  case NotamCount::TIME:
    return stats.filtered_by_time;
  case NotamCount::RADIUS:
    return stats.filtered_by_radius;
  case NotamCount::QCODE:
    return stats.filtered_by_qcode;
  }

  return 0;
}

static void
AddCount(GroupedListWidget &list, const std::shared_ptr<NotamFields> &fields,
         NotamCount kind) noexcept
{
  list.AddValue("", nullptr,
                [fields, kind](GroupedListWidget::ValueState &state) {
                  state.hidden = !fields->enabled;
                  if (fields->loading) {
                    state.text = C_("Status", "Loading...");
                    return;
                  }

                  char buffer[64];
                  StringFormat(buffer, ARRAY_SIZE(buffer),
                               _("%u filtered"), FilteredCount(kind));
                  state.text = buffer;
                });
}

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

static void
SaveNotam(NotamFields &fields, bool &changed)
{
  AirspaceComputerSettings &computer =
    CommonInterface::SetComputerSettings().airspace;
  const bool was_enabled = computer.notam.enabled;
  const unsigned old_radius_km = computer.notam.radius_km;
  const auto old_api_url = computer.notam.api_base_url;

  ConfigPanel::CommitSetting(changed, computer.notam.enabled, fields.enabled,
                             ProfileKeys::NOTAMEnabled);
  if (computer.notam.api_base_url != fields.api_url) {
    computer.notam.api_base_url = fields.api_url;
    Profile::Set(ProfileKeys::NOTAMApiUrl,
                 computer.notam.api_base_url.c_str());
    changed = true;
  }

  if (fields.refresh_min >= 0) {
    const auto refresh = static_cast<unsigned>(fields.refresh_min);
    ConfigPanel::CommitSetting(changed, computer.notam.refresh_interval_min,
                               refresh, ProfileKeys::NOTAMRefreshInterval);
  }

  const unsigned clamped_refresh_interval =
    std::clamp(computer.notam.refresh_interval_min, 0u,
               MAX_NOTAM_REFRESH_INTERVAL_MIN);
  if (computer.notam.refresh_interval_min != clamped_refresh_interval) {
    computer.notam.refresh_interval_min = clamped_refresh_interval;
    Profile::Set(ProfileKeys::NOTAMRefreshInterval, clamped_refresh_interval);
    changed = true;
  }

  unsigned radius_km = static_cast<unsigned>(
    std::lround(Units::ToSysDistance(fields.radius_user) / 1000.0));
  if (radius_km < 1)
    radius_km = 1;
  if (radius_km > MAX_NOTAM_REQUEST_RADIUS_KM)
    radius_km = MAX_NOTAM_REQUEST_RADIUS_KM;
  if (computer.notam.radius_km != radius_km) {
    computer.notam.radius_km = radius_km;
    Profile::Set(ProfileKeys::NOTAMRadius, radius_km);
    changed = true;
  }

  const bool show_ifr_changed =
    ConfigPanel::CommitSetting(changed, computer.notam.show_ifr,
                               fields.show_ifr, ProfileKeys::NOTAMShowIFR);
  const bool show_only_effective_changed =
    ConfigPanel::CommitSetting(changed, computer.notam.show_only_effective,
                               fields.show_only_effective,
                               ProfileKeys::NOTAMShowOnlyEffective);
  const bool filter_flags_changed =
    show_ifr_changed || show_only_effective_changed;

  const unsigned max_radius_m = static_cast<unsigned>(
    std::lround(Units::ToSysDistance(fields.max_radius_user)));
  bool max_radius_changed = false;
  if (computer.notam.max_radius_m != max_radius_m) {
    computer.notam.max_radius_m = max_radius_m;
    Profile::Set(ProfileKeys::NOTAMMaxRadius, max_radius_m);
    changed = true;
    max_radius_changed = true;
  }

  bool qcodes_changed = false;
  if (computer.notam.hidden_qcodes != fields.hidden_qcodes) {
    computer.notam.hidden_qcodes = fields.hidden_qcodes;
    Profile::Set(ProfileKeys::NOTAMHiddenQCodes,
                 computer.notam.hidden_qcodes.c_str());
    qcodes_changed = true;
    changed = true;
  }

  const bool radius_changed = old_radius_km != computer.notam.radius_km;
  const bool api_url_changed = old_api_url != computer.notam.api_base_url;

  if (net_components != nullptr && net_components->notam != nullptr)
    net_components->notam->SetSettings(computer.notam);

  if (was_enabled && !computer.notam.enabled) {
    if (net_components != nullptr && net_components->notam != nullptr) {
      try {
        const ScopeSuspendAllThreads suspend;
        net_components->notam->Clear();
        if (data_components != nullptr && data_components->airspaces != nullptr)
          net_components->notam->UpdateAirspaces(*data_components->airspaces);
      } catch (const std::exception &e) {
        LogFmt("Failed to clear NOTAMs after disabling: {}", e.what());
      } catch (...) {
        LogError(std::current_exception(),
                 "Failed to clear NOTAMs after disabling");
      }

      try {
        net_components->notam->InvalidateCache();
      } catch (const std::exception &e) {
        LogFmt("Failed to invalidate NOTAM cache: {}", e.what());
      } catch (...) {
        LogError(std::current_exception(),
                 "Failed to invalidate NOTAM cache");
      }
    }
  } else {
    const bool filters_changed =
      filter_flags_changed || max_radius_changed || qcodes_changed;
    if (net_components != nullptr && net_components->notam != nullptr &&
        data_components != nullptr && data_components->airspaces != nullptr &&
        computer.notam.enabled) {
      try {
        const bool enabled_changed = !was_enabled && computer.notam.enabled;
        if ((enabled_changed || radius_changed || api_url_changed) &&
            !fields.saving_for_manual) {
          const auto &basic = CommonInterface::Basic();
          if (basic.location_available && basic.location.IsValid())
            net_components->notam->ForceUpdateLocation(basic.location, true);
        }

        if (filters_changed) {
          const ScopeSuspendAllThreads suspend;
          net_components->notam->UpdateAirspaces(*data_components->airspaces);
          if (data_components->terrain != nullptr)
            SetAirspaceGroundLevels(*data_components->airspaces,
                                    *data_components->terrain);
        }
      } catch (const std::exception &e) {
        LogFmt("Failed to apply NOTAM settings changes: {}", e.what());
      } catch (...) {
        LogError(std::current_exception(),
                 "Failed to apply NOTAM settings changes");
      }
    }
  }
}

struct NotamWatch final : NOTAMListener {
  std::shared_ptr<NotamFields> fields;
  GroupedListWidget *page = nullptr;
  UI::Notify notify;
  bool registered = false;

  explicit NotamWatch(std::shared_ptr<NotamFields> _fields) noexcept
    :fields(std::move(_fields)),
     notify([this] {
       fields->loading = false;
       if (page != nullptr)
         page->UpdateValues();
     }) {}

  ~NotamWatch() noexcept override {
    Unregister();
  }

  void Register() noexcept {
    if (registered || net_components == nullptr ||
        net_components->notam == nullptr)
      return;

    try {
      net_components->notam->AddListener(*this);
      registered = true;
    } catch (const std::exception &e) {
      LogFmt("Failed to register NOTAM config listener: {}", e.what());
    } catch (...) {
      LogError(std::current_exception(),
               "Failed to register NOTAM config listener");
    }
  }

  void Unregister() noexcept {
    if (!registered)
      return;

    if (net_components != nullptr && net_components->notam != nullptr)
      net_components->notam->RemoveListener(*this);
    registered = false;
    notify.ClearNotification();
  }

  void OnNOTAMsUpdated() noexcept override {
    notify.SendNotification();
  }

  void OnNOTAMsLoadComplete(NOTAMLoadNotification) noexcept override {
    notify.SendNotification();
  }
};

static void
OnUpdateButton(NotamFields &fields, GroupedListWidget &page)
{
  LogFormat("NOTAM: Manual update triggered from settings panel");
  const unsigned old_radius_km =
    CommonInterface::GetComputerSettings().airspace.notam.radius_km;
  const auto old_api_url =
    CommonInterface::GetComputerSettings().airspace.notam.api_base_url;

  fields.saving_for_manual = true;
  bool dummy_changed = false;
  page.Save(dummy_changed);
  fields.saving_for_manual = false;

  const auto &computer_settings = CommonInterface::GetComputerSettings();
  const bool notam_enabled = computer_settings.airspace.notam.enabled;
  const bool radius_changed =
    old_radius_km != computer_settings.airspace.notam.radius_km;
  const bool api_url_changed =
    old_api_url != computer_settings.airspace.notam.api_base_url;

  if (net_components == nullptr || net_components->notam == nullptr ||
      !notam_enabled)
    return;

  const auto &basic = CommonInterface::Basic();
  if (!basic.location_available || !basic.location.IsValid()) {
    page.UpdateValues();
    ShowMessageBox(_("No valid location."), C_("Menu", "NOTAM"),
                   MB_OK | MB_ICONEXCLAMATION);
    return;
  }

  net_components->notam->ResetFetchFailureNotification();
  if (net_components->notam->ForceUpdateLocation(basic.location,
                                                 radius_changed ||
                                                 api_url_changed)) {
    fields.loading = true;
    page.UpdateValues();
    net_components->notam->MarkManualRefreshRequested();
  }
}

#endif

std::unique_ptr<Widget>
CreateNOTAMConfigPanel()
{
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());

#ifdef HAVE_HTTP
  const auto &computer = CommonInterface::GetComputerSettings().airspace;
  const Unit distance_unit = Units::GetUserDistanceUnit();
  const char *unit_name = Units::GetUnitName(distance_unit);

  auto fields = std::make_shared<NotamFields>();
  fields->enabled = computer.notam.enabled;
  fields->api_url = computer.notam.api_base_url;
  fields->radius_user =
    Units::ToUserDistance(computer.notam.radius_km * 1000.0);
  fields->refresh_min = static_cast<int>(computer.notam.refresh_interval_min);
  fields->show_ifr = computer.notam.show_ifr;
  fields->show_only_effective = computer.notam.show_only_effective;
  fields->max_radius_user = Units::ToUserDistance(computer.notam.max_radius_m);
  fields->hidden_qcodes = computer.notam.hidden_qcodes;
  fields->search_min = Units::ToUserDistance(1000.0);
  fields->search_max =
    Units::ToUserDistance(MAX_NOTAM_REQUEST_RADIUS_KM * 1000.0);
  fields->search_step = Units::ToUserDistance(10000.0);
  StringFormat(fields->distance_format, sizeof(fields->distance_format),
               _("%%.0f %s"), unit_name);

  auto watch = std::make_shared<NotamWatch>(fields);
  GroupedListWidget *page = list.get();
  watch->page = page;
  const auto shown = [fields] { return fields->enabled; };

  list->AddGroup(nullptr);
  AddToggle(*list, page, _("NOTAM Support"),
            _("Enable downloading and display of NOTAMs from aviation authorities."),
            fields->enabled);

  GroupedListWidget::ItemOptions notice;
  notice.description =
    _("Notice: NOTAM display is for situational awareness only\n"
      "and does not replace proper pre-flight NOTAM briefing.");
  notice.value_callback = [fields](GroupedListWidget::ValueState &state) {
    state.hidden = !fields->enabled;
  };
  list->AddItem(nullptr, notice);

  list->AddText(_("API URL"),
                _("Base URL of the NOTAM proxy API. Must be configured before NOTAMs can be fetched."),
                fields->api_url.data(), fields->api_url.capacity(),
                false, shown);
  list->AddFloat(_("Search Radius"),
                 _("Radius around current location to fetch NOTAMs."),
                 fields->distance_format, "%.0f",
                 fields->search_min, fields->search_max, fields->search_step,
                 false, fields->radius_user, false, shown);
  list->AddInteger(_("Auto-Refresh (minutes)"),
                   _("Automatically refresh NOTAMs every X minutes. Set to 0 to disable."),
                   _("%d min"), "%d", 0, MAX_NOTAM_REFRESH_INTERVAL_MIN, 15,
                   fields->refresh_min, false, shown);

  list->AddGroup(nullptr);
  list->AddSwitch(_("Show IFR-Only NOTAMs"),
                  _("Include NOTAMs for IFR traffic only."),
                  fields->show_ifr, false, shown);
  AddCount(*list, fields, NotamCount::IFR);
  list->AddSwitch(_("Show Only Currently Effective"),
                  _("Filter out NOTAMs not currently in effect."),
                  fields->show_only_effective, false, shown);
  AddCount(*list, fields, NotamCount::TIME);
  list->AddFloat(_("Maximum NOTAM Radius"),
                 _("Filter out NOTAMs with radius larger than this. Set to 0 to disable."),
                 fields->distance_format, "%.0f",
                 0, fields->search_max, fields->search_step,
                 false, fields->max_radius_user, false, shown);
  AddCount(*list, fields, NotamCount::RADIUS);
  list->AddText(_("Hidden Q-Codes"),
                _("Space-separated Q-code prefixes to hide (e.g., QA QK QN QOA QOL)."),
                fields->hidden_qcodes.data(), fields->hidden_qcodes.capacity(),
                false, shown);
  AddCount(*list, fields, NotamCount::QCODE);

  list->SetVisibilityCallback([watch, page](bool visible) {
    if (!visible) {
      watch->Unregister();
      ConfigPanel::ReturnExtraButton(1);
      ConfigPanel::ReturnExtraButton(2);
      return;
    }

    watch->Register();
    ConfigPanel::BorrowExtraButton(1, _("Refresh"), [watch, page] {
      OnUpdateButton(*watch->fields, *page);
    });
    ConfigPanel::BorrowExtraButton(2, _("List"), [watch, page] {
      ShowNOTAMListDialog(UIGlobals::GetMainWindow());

      /* Filtering from the list changes the shared settings directly.
         This panel remains alive while the list dialog is open, so
         reload the text field to avoid saving its stale value. */
      watch->fields->hidden_qcodes =
        CommonInterface::GetComputerSettings().airspace.notam.hidden_qcodes;
      page->UpdateValues();
    });
  });

  list->SetSaveCallback([fields](bool &changed) {
    SaveNotam(*fields, changed);
    return true;
  });
#endif

  return list;
}
