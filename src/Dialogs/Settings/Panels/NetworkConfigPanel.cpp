// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "NetworkConfigPanel.hpp"
#include "Dialogs/Message.hpp"
#include "Dialogs/WifiDialog.hpp"
#include "Language/Language.hpp"
#include "Language/FormatText.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "net/State.hpp"
#include "system/OpenLink.hpp"
#include "util/StaticString.hxx"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(KOBO)
#include "Kobo/PlatformWifiBackend.hpp"
#include "Kobo/System.hpp"
#include "net/wifi/WifiError.hpp"
#endif

#if defined(HAVE_LINUX_NET_WIFI)
#include "net/wifi/LinuxWifiBackend.hpp"
#include "net/wifi/WifiError.hpp"
#endif

#ifdef ANDROID
#include "Android/Main.hpp"
#include "Android/NativeView.hpp"
#include "java/Global.hxx"
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
#include "net/IPv4Address.hxx"
#endif

struct NetworkConfigState {
  NetState connectivity{NetState::UNKNOWN};
  StaticString<256> status{_("Unknown")};
  StaticString<64> ip{_("Unknown")};
  StaticString<64> backend{_("Unknown")};
  bool have_radio_enabled{false};
  bool radio_enabled{false};
  bool have_persist_wifi_enabled{false};
  bool persist_wifi_enabled{false};
};

#if defined(HAVE_LINUX_NET_WIFI)
static const char *
LinuxBackendName(LinuxWifiBackendKind backend_kind) noexcept
{
  switch (backend_kind) {
  case LinuxWifiBackendKind::None:
    return _("None");
  case LinuxWifiBackendKind::NetworkManager:
    return "NetworkManager";
  case LinuxWifiBackendKind::ConnMan:
    return "ConnMan";
  }

  return _("Unknown");
}
#endif

#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IPHONE)
static StaticString<64>
GetPlatformWifiIpAddress() noexcept
{
  StaticString<64> text;
  text.clear();

#ifdef ANDROID
  if (native_view == nullptr)
    return text;

  native_view->GetWifiIpAddress(Java::GetEnv(),
                                text.buffer(), text.capacity());
#else
  char buffer[64];
  const auto address = IPv4Address::GetDeviceAddress("en0");
  if (address.IsDefined() &&
      address.ToString(buffer, sizeof(buffer)) != nullptr)
    text = buffer;
#endif

  return text;
}
#endif

#if defined(KOBO) || defined(HAVE_LINUX_NET_WIFI)
static const char *
GetWifiServiceUnavailableText() noexcept
{
  return _("WiFi service is not available.");
}
#endif

#if defined(ANDROID) || defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_IPHONE)
static const char *
GetManagedBySystemSettingsText() noexcept
{
  return _("Managed by system settings.");
}
#endif

static const char *
GetStatusHelp() noexcept
{
#if defined(KOBO) || defined(HAVE_LINUX_NET_WIFI)
  return _("This page shows WiFi status. Use WiFi list to scan and connect.");
#elif defined(ANDROID) || defined(_WIN32)
  return _("WiFi is managed by the system settings. Use WiFi list to open them.");
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  return _("WiFi is managed by the system settings. Use WiFi list for instructions.");
#else
  return _("Network details are not available in this build.");
#endif
}

static const char *
GetBackendHelp() noexcept
{
#if defined(KOBO) || defined(HAVE_LINUX_NET_WIFI) || defined(ANDROID) || defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  return _("WiFi service used by the device.");
#else
  static StaticString<128> message;
  FormatFeatureNotAvailableInThisBuild(message,
                                       _("Platform/backend information"));
  return message.c_str();
#endif
}

static bool
PlatformHasRadio() noexcept
{
#if defined(KOBO)
  return true;
#elif defined(HAVE_LINUX_NET_WIFI)
  try {
    return HasLinuxWifiRadioToggle(QueryLinuxWifiBackendKind());
  } catch (...) {
    return false;
  }
#else
  return false;
#endif
}

static void
ApplyRadio(bool enabled, const std::function<void()> &refresh) noexcept
{
#if defined(KOBO)
  try {
    const bool success = enabled ? KoboWifiOn() : KoboWifiOff();
    if (!success)
      throw std::runtime_error{enabled
        ? _("Failed to enable WiFi.")
        : _("Failed to disable WiFi.")};
  } catch (...) {
    const auto message = WifiError::Format(std::current_exception());
    ShowMessageBox(message.c_str(), _("Network"), MB_OK);
  }
#elif defined(HAVE_LINUX_NET_WIFI)
  try {
    const auto backend_kind = QueryLinuxWifiBackendKind();
    if (backend_kind != LinuxWifiBackendKind::None)
      SetLinuxWifiRadioEnabled(backend_kind, enabled);
  } catch (...) {
    const auto message = WifiError::Format(std::current_exception());
    ShowMessageBox(message.c_str(), _("Network"), MB_OK);
  }
#else
  (void)enabled;
#endif
  refresh();
}

static void
ApplyPersistWifi(bool enabled, const std::function<void()> &refresh) noexcept
{
#if defined(KOBO)
  if (!SetKoboWifiAutoOn(enabled)) {
    ShowMessageBox(_("Failed to store the WiFi startup setting."),
                   _("Network"), MB_OK);
    refresh();
  }
#else
  (void)enabled;
  (void)refresh;
#endif
}

static void
OpenPlatformWifiList(std::function<void()> refresh) noexcept
{
#if defined(KOBO)
  try {
    auto backend = CreatePlatformWifiBackend();
    if (backend == nullptr) {
      ShowMessageBox(GetWifiServiceUnavailableText(), _("Network"), MB_OK);
      return;
    }

    ShowWifiDialog(std::move(backend));
    refresh();
  } catch (...) {
    const auto message = WifiError::Format(std::current_exception());
    ShowMessageBox(message.c_str(), _("Network"), MB_OK);
  }
#elif defined(HAVE_LINUX_NET_WIFI)
  try {
    auto backend = CreateLinuxWifiBackend();
    if (backend == nullptr) {
      ShowMessageBox(GetWifiServiceUnavailableText(), _("Network"), MB_OK);
      return;
    }

    ShowWifiDialog(std::move(backend));
    refresh();
  } catch (...) {
    const auto message = WifiError::Format(std::current_exception());
    ShowMessageBox(message.c_str(), _("Network"), MB_OK);
  }
#elif defined(ANDROID)
  if (native_view != nullptr && native_view->OpenWifiSettings(Java::GetEnv()))
    return;

  ShowMessageBox(_("Failed to open system settings."),
                 C_("Setting", "Connectivity"), MB_OK);
#elif defined(_WIN32)
  if (OpenLink("ms-settings:network-wifi"))
    return;

  ShowMessageBox(_("Failed to open system settings."),
                 C_("Setting", "Connectivity"), MB_OK);
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  ShowMessageBox(_("Open the Settings app, then go to Wi-Fi."),
                 C_("Setting", "Connectivity"), MB_OK);
#else
  (void)refresh;
  {
    StaticString<128> message;
    FormatFeatureNotAvailableInThisBuild(message, C_("Setting", "WiFi management"));
    ShowMessageBox(message, C_("Setting", "Connectivity"), MB_OK);
  }
#endif

#if defined(ANDROID) || defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  (void)refresh;
#endif
}

static void
BuildPlatformState(NetworkConfigState &state, bool have_radio) noexcept
{
#if defined(KOBO)
  state.connectivity = GetNetState();
  state.backend = "wpa_supplicant";
  state.have_radio_enabled = true;
  state.radio_enabled = IsKoboWifiOn();
  state.have_persist_wifi_enabled = true;
  state.persist_wifi_enabled = IsKoboWifiAutoOn();

  if (!state.radio_enabled) {
    state.status = _("Disabled");
    return;
  }

  try {
    auto backend = CreatePlatformWifiBackend();
    if (backend == nullptr) {
      state.status = GetWifiServiceUnavailableText();
      return;
    }

    const auto status = backend->GetBackendStatus();
    state.status = WifiBackendStatus::Format(status);
    state.ip = WifiBackendStatus::FormatIpAddress(status);
  } catch (...) {
    const auto message = WifiError::Format(std::current_exception());
    state.status = message.c_str();
  }
#elif defined(HAVE_LINUX_NET_WIFI)
  try {
    const auto backend_kind = QueryLinuxWifiBackendKind();
    state.connectivity = GetNetState();
    state.backend = LinuxBackendName(backend_kind);

    auto backend = CreateLinuxWifiBackend(backend_kind);
    if (backend == nullptr) {
      state.status = GetWifiServiceUnavailableText();
    } else {
      const auto status = backend->GetBackendStatus();
      state.status = WifiBackendStatus::Format(status);
      state.ip = WifiBackendStatus::FormatIpAddress(status);
    }

    if (have_radio) {
      state.have_radio_enabled = true;
      state.radio_enabled = GetLinuxWifiRadioEnabled(backend_kind);
    }
  } catch (...) {
    const auto message = WifiError::Format(std::current_exception());
    state.status = message.c_str();
  }
#elif defined(ANDROID)
  state.connectivity = GetNetState();
  state.backend = "Android";
  state.status = GetManagedBySystemSettingsText();
  const auto android_ip = GetPlatformWifiIpAddress();
  if (!android_ip.empty())
    state.ip = android_ip;
#elif defined(_WIN32)
  state.connectivity = GetNetState();
  state.backend = "Windows";
  state.status = GetManagedBySystemSettingsText();
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  state.connectivity = GetNetState();
  state.backend = "iOS";
  state.status = GetManagedBySystemSettingsText();
  const auto ios_ip = GetPlatformWifiIpAddress();
  if (!ios_ip.empty())
    state.ip = ios_ip;
#else
  (void)have_radio;
  state.backend = C_("Status", "Unavailable");
  state.status = _("In-app network settings are not available in this build.");
#endif

#if defined(KOBO) || defined(ANDROID) || defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  (void)have_radio;
#endif
}

static void
AddToggle(GroupedListWidget &list, unsigned index,
          const char *caption, const char *help, bool checked,
          std::function<void(bool enabled)> apply) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.toggle = true;
  options.checked = checked;
  options.help = help;
  list.AddItem(caption, [&list, index, apply = std::move(apply)] {
    apply(list.IsItemChecked(index));
  }, options);
}

std::unique_ptr<Widget>
CreateNetworkConfigPanel()
{
  struct Fields {
    NetworkConfigState state;
    bool have_radio = false;
    bool have_persist = false;
    unsigned radio_index = 0;
    unsigned persist_index = 0;
  };

  auto fields = std::make_shared<Fields>();
  fields->have_radio = PlatformHasRadio();
#if defined(KOBO)
  fields->have_persist = true;
#endif
  BuildPlatformState(fields->state, fields->have_radio);

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();

  auto refresh = std::make_shared<std::function<void()>>();
  *refresh = [fields, page] {
    BuildPlatformState(fields->state, fields->have_radio);
    page->UpdateValues();
    if (fields->have_radio && fields->state.have_radio_enabled)
      page->SetItemChecked(fields->radio_index, fields->state.radio_enabled);
    if (fields->have_persist && fields->state.have_persist_wifi_enabled)
      page->SetItemChecked(fields->persist_index,
                           fields->state.persist_wifi_enabled);
  };

  list->AddGroup(nullptr);
  list->AddValue(_("Status"), GetStatusHelp(),
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = fields->state.status.c_str();
                 });
  list->AddValue(C_("Setting", "Connectivity"),
                 _("Current network connectivity state."),
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = NetStateText::ToString(fields->state.connectivity);
                 });
  list->AddValue(_("IP address"),
                 _("IPv4 address of the active WiFi interface."),
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = fields->state.ip.c_str();
                 });
  list->AddValue(C_("Setting", "Backend"), GetBackendHelp(),
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = fields->state.backend.c_str();
                 });

  if (fields->have_radio) {
    fields->radio_index = list->GetItemCount();
    AddToggle(*list, fields->radio_index,
              C_("Setting", "WiFi Enabled"),
#if defined(KOBO)
              _("Turns the Kobo WiFi interface on or off."),
#else
              nullptr,
#endif
              fields->state.radio_enabled,
              [refresh](bool enabled) { ApplyRadio(enabled, *refresh); });
  }

  if (fields->have_persist) {
    fields->persist_index = list->GetItemCount();
    AddToggle(*list, fields->persist_index,
              C_("Setting", "Auto WiFi"),
              _("Enable WiFi automatically at startup."),
              fields->state.persist_wifi_enabled,
              [refresh](bool enabled) {
                ApplyPersistWifi(enabled, *refresh);
              });
  }

  list->AddButton(C_("Button", "WiFi List"), [refresh] {
    OpenPlatformWifiList(*refresh);
  });

  list->SetVisibilityCallback([refresh](bool visible) {
    if (visible)
      (*refresh)();
  });

  (*refresh)();
  return list;
}
