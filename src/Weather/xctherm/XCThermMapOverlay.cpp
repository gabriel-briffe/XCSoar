// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "XCThermMapOverlay.hpp"
#include "XCThermAPI.hpp"
#include "XCThermCatalog.hpp"
#include "XCThermForecastTime.hpp"
#include "XCThermDownloadGlue.hpp"
#include "XCThermDownloadJob.hpp"
#include "XCThermGeoJSONOverlay.hpp"
#include "ActionInterface.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "LogFile.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "UIGlobals.hpp"
#include "Weather/Settings.hpp"
#include "net/http/Init.hpp"
#include "ui/event/Notify.hpp"
#include "util/Macros.hpp"
#include "util/TruncateString.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace XCTherm {

namespace {

std::string auto_fetch_attempted_param;

#ifdef ENABLE_OPENGL

/** Parsed forecast kept after ClearMapOverlay() for fast page return. */
struct ParsedLayerCache {
  std::string parameter;
  std::string label;
  unsigned utc_hour = 0;
  XCThermGeoJSON::ForecastLayer forecast;

  bool Matches(const char *parameter, unsigned utc_hour) const noexcept
  {
    return !forecast.IsEmpty() && parameter != nullptr &&
           this->parameter == parameter && this->utc_hour == utc_hour;
  }

  void Store(XCThermGeoJSON::ForecastLayer &&layer, std::string label,
             std::string parameter, unsigned utc_hour) noexcept
  {
    forecast = std::move(layer);
    this->label = std::move(label);
    this->parameter = std::move(parameter);
    this->utc_hour = utc_hour;
  }

  void Clear() noexcept
  {
    parameter.clear();
    label.clear();
    utc_hour = 0;
    forecast = {};
  }

  bool TakeIfMatches(const char *parameter, unsigned utc_hour,
                     XCThermGeoJSON::ForecastLayer &out_forecast) noexcept
  {
    if (!Matches(parameter, utc_hour))
      return false;

    out_forecast = std::move(forecast);
    Clear();
    return true;
  }
};

static ParsedLayerCache parsed_layer_cache;

/**
 * Background GeoJSON parse/cleanup.  Keeps the current overlay until
 * the latest request finishes, then swaps on the UI thread (no spinner).
 * Rapid time/altitude steps keep only the newest pending request.
 */
struct AsyncParseRequest {
  std::string geojson;
  std::string label;
  std::string parameter;
  unsigned forecast_utc = 0;
  unsigned generation = 0;
};

struct AsyncParseState {
  std::mutex mutex;
  std::atomic<unsigned> generation{0};
  std::optional<AsyncParseRequest> pending;
  bool worker_running = false;

  unsigned result_generation = 0;
  bool result_ok = false;
  std::string result_label;
  std::string result_parameter;
  unsigned result_utc = 0;
  XCThermGeoJSON::ForecastLayer result_forecast;
};

static AsyncParseState async_parse;

static void OnAsyncParseNotify() noexcept;

static UI::Notify async_parse_notify{[]{ OnAsyncParseNotify(); }};

static void
InvalidateAsyncParse() noexcept
{
  async_parse.generation.fetch_add(1, std::memory_order_relaxed);
  {
    const std::lock_guard lock{async_parse.mutex};
    async_parse.pending.reset();
    async_parse.result_ok = false;
    async_parse.result_forecast = {};
  }
  async_parse_notify.ClearNotification();
}

static void
AsyncParseWorker() noexcept
{
  for (;;) {
    AsyncParseRequest request;
    {
      const std::lock_guard lock{async_parse.mutex};
      if (!async_parse.pending.has_value()) {
        async_parse.worker_running = false;
        return;
      }

      request = std::move(*async_parse.pending);
      async_parse.pending.reset();
    }

    if (request.generation !=
        async_parse.generation.load(std::memory_order_relaxed))
      continue;

    auto forecast = XCThermGeoJSON::Parse(request.geojson, true);
    if (request.generation !=
        async_parse.generation.load(std::memory_order_relaxed))
      continue;

    if (!forecast.IsEmpty())
      forecast.layer_name = request.label;

    {
      const std::lock_guard lock{async_parse.mutex};
      if (request.generation !=
          async_parse.generation.load(std::memory_order_relaxed))
        continue;

      async_parse.result_generation = request.generation;
      async_parse.result_ok = !forecast.IsEmpty();
      async_parse.result_label = std::move(request.label);
      async_parse.result_parameter = std::move(request.parameter);
      async_parse.result_utc = request.forecast_utc;
      async_parse.result_forecast = std::move(forecast);
    }

    async_parse_notify.SendNotification();
  }
}

static void
ScheduleAsyncParse(std::string geojson, std::string label,
                   std::string parameter, unsigned forecast_utc) noexcept
{
  AsyncParseRequest request;
  request.geojson = std::move(geojson);
  request.label = std::move(label);
  request.parameter = std::move(parameter);
  request.forecast_utc = forecast_utc;
  request.generation =
    async_parse.generation.fetch_add(1, std::memory_order_relaxed) + 1;

  {
    const std::lock_guard lock{async_parse.mutex};
    async_parse.pending = std::move(request);
    if (async_parse.worker_running)
      return;

    async_parse.worker_running = true;
  }

  std::thread(AsyncParseWorker).detach();
}

static void
OnAsyncParseNotify() noexcept
{
  XCThermGeoJSON::ForecastLayer forecast;
  std::string label;
  std::string parameter;
  unsigned forecast_utc = 0;
  bool ok = false;

  {
    const std::lock_guard lock{async_parse.mutex};
    if (async_parse.result_generation !=
        async_parse.generation.load(std::memory_order_relaxed))
      return;

    ok = async_parse.result_ok;
    label = std::move(async_parse.result_label);
    parameter = std::move(async_parse.result_parameter);
    forecast_utc = async_parse.result_utc;
    forecast = std::move(async_parse.result_forecast);
    async_parse.result_ok = false;
  }

  if (!ok) {
    LogFmt("xctherm: async parse failed for {}", label);
    return;
  }

  ApplyForecastLayerToMap(std::move(forecast), label.c_str(),
                          parameter.empty() ? nullptr : parameter.c_str(),
                          forecast_utc);
}

#endif /* ENABLE_OPENGL */

static bool
MapShowsXCThermForecast(const char *parameter, unsigned utc_hour) noexcept
{
#ifdef ENABLE_OPENGL
  auto *map = UIGlobals::GetMap();
  if (map == nullptr)
    return false;

  const auto *overlay =
    dynamic_cast<const XCThermGeoJSONOverlay *>(map->GetOverlay());
  if (overlay == nullptr)
    return false;

  return overlay->MatchesForecast(parameter, utc_hour);
#else
  (void)parameter;
  (void)utc_hour;
  return false;
#endif
}

} // namespace

bool
MapShowsForecast(const char *parameter, unsigned utc_hour) noexcept
{
  return MapShowsXCThermForecast(parameter, utc_hour);
}

bool
HasActiveMapOverlay() noexcept
{
#ifdef ENABLE_OPENGL
  const auto *map = UIGlobals::GetMap();
  if (map == nullptr)
    return false;

  return dynamic_cast<const XCThermGeoJSONOverlay *>(map->GetOverlay()) !=
    nullptr;
#else
  return false;
#endif
}

void
ApplyForecastToMap(const std::string &geojson, const char *label,
                   const char *parameter, const unsigned forecast_utc) noexcept
{
  if (parameter != nullptr &&
      MapShowsXCThermForecast(parameter, forecast_utc))
    return;

#ifdef ENABLE_OPENGL
  if (parameter != nullptr) {
    XCThermGeoJSON::ForecastLayer cached_forecast;
    if (parsed_layer_cache.TakeIfMatches(parameter, forecast_utc,
                                         cached_forecast)) {
      InvalidateAsyncParse();
      ApplyForecastLayerToMap(std::move(cached_forecast), label,
                              parameter, forecast_utc);
      return;
    }
  }

  /* Keep the current overlay; parse/cleanup off the UI thread, then swap. */
  ScheduleAsyncParse(geojson,
                     label != nullptr ? label : "",
                     parameter != nullptr ? parameter : "",
                     forecast_utc);
#else
  auto forecast = XCThermGeoJSON::Parse(geojson, true);
  if (forecast.IsEmpty()) {
    LogFmt("xctherm: parse failed for {}", label);
    return;
  }

  forecast.layer_name = label;
  ApplyForecastLayerToMap(std::move(forecast), label, parameter, forecast_utc);
#endif
}

void
ApplyForecastLayerToMap(XCThermGeoJSON::ForecastLayer &&forecast,
                        const char *label, const char *parameter,
                        const unsigned forecast_utc) noexcept
{
  if (forecast.IsEmpty())
    return;

  if (parameter != nullptr &&
      MapShowsXCThermForecast(parameter, forecast_utc))
    return;

  auto *map = UIGlobals::GetMap();
  if (map == nullptr)
    return;

#ifdef ENABLE_OPENGL
  InvalidateAsyncParse();

  auto overlay = std::make_unique<XCThermGeoJSONOverlay>();
  overlay->SetForecast(std::move(forecast), label, parameter, forecast_utc);
  overlay->SetOpacityPercent(
    CommonInterface::GetComputerSettings().weather.xctherm.opacity_percent);
  map->SetOverlay(std::move(overlay));

  /* Map bottom title follows the installed overlay (not the cursor). */
  auto &cursor = CommonInterface::SetUIState().weather.xctherm_cursor;
  cursor.has_displayed = true;
  cursor.displayed_utc_hour = forecast_utc;
  if (label != nullptr && label[0] != '\0')
    CopyTruncateString(cursor.displayed_label,
                       ARRAY_SIZE(cursor.displayed_label), label);
  else
    cursor.displayed_label[0] = '\0';
  ActionInterface::SendUIState(true);
#else
  (void)label;
  (void)parameter;
  (void)forecast_utc;
#endif
}

void
ApplyOverlayOpacityFromSettings() noexcept
{
#ifdef ENABLE_OPENGL
  auto *map = UIGlobals::GetMap();
  if (map == nullptr)
    return;

  auto *overlay =
    const_cast<XCThermGeoJSONOverlay *>(
      dynamic_cast<const XCThermGeoJSONOverlay *>(map->GetOverlay()));
  if (overlay == nullptr)
    return;

  overlay->SetOpacityPercent(
    CommonInterface::GetComputerSettings().weather.xctherm.opacity_percent);
#endif
}

void
ClearMapOverlay() noexcept
{
#ifdef ENABLE_OPENGL
  InvalidateAsyncParse();

  auto *map = UIGlobals::GetMap();
  if (map == nullptr)
    return;

  const auto *overlay =
    dynamic_cast<const XCThermGeoJSONOverlay *>(map->GetOverlay());
  if (overlay == nullptr)
    return;

  std::string label, parameter;
  unsigned utc_hour = 0;
  auto forecast = const_cast<XCThermGeoJSONOverlay *>(overlay)
    ->TakeForecast(label, parameter, utc_hour);
  if (!forecast.IsEmpty())
    parsed_layer_cache.Store(std::move(forecast), std::move(label),
                               std::move(parameter), utc_hour);

  map->SetOverlay(nullptr);

  CommonInterface::SetUIState().weather.xctherm_cursor.ClearDisplayed();
  ActionInterface::SendUIState(true);
#endif
}

void
ClearParsedLayerCache(const std::string *parameter) noexcept
{
#ifdef ENABLE_OPENGL
  if (parameter == nullptr || parsed_layer_cache.parameter == *parameter)
    parsed_layer_cache.Clear();
#else
  (void)parameter;
#endif
}

void
ApplyCursorOverlayFromSession() noexcept
{
  const auto &weather = CommonInterface::GetUIState().weather;
  const auto &session = weather.xctherm;
  const auto &cursor = weather.xctherm_cursor;
  if (!session.cursor_initialized) {
    RestoreActiveLayerOverlay();
    return;
  }

  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  const auto &region = GetRegion(settings.model);
  if (cursor.layer >= region.layer_count)
    return;

  const auto &layer = region.layers[cursor.layer];
  if (!XCThermAPI::Instance().IsLayerCached(layer.api_parameter,
                                             cursor.forecast_utc_hour)) {
    ClearMapOverlay();
    return;
  }

  ApplyCachedLayerOverlay(cursor.layer, cursor.forecast_utc_hour);
}

bool
EnterDedicatedPage() noexcept
{
  return CommonInterface::SetUIState().weather.xctherm.EnterPage();
}

void
LeaveDedicatedPage() noexcept
{
  CommonInterface::SetUIState().weather.xctherm.LeavePage();
}

void
SuspendDedicatedPageForPan() noexcept
{
  CommonInterface::SetUIState().weather.xctherm.SuspendForPan();
}

void
ResumeDedicatedPageAfterPan() noexcept
{
  CommonInterface::SetUIState().weather.xctherm.ResumeAfterPan();
}

bool
IsDedicatedPageSuspendedForPan() noexcept
{
  return CommonInterface::GetUIState().weather.xctherm.IsSuspendedForPan();
}

void
FormatLayerTitleLabel(StaticString<64> &text) noexcept
{
  text.clear();

  const auto &weather = CommonInterface::GetUIState().weather;
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  const auto &region = GetRegion(settings.model);

  unsigned layer_index = 0;
  if (weather.xctherm.cursor_initialized) {
    if (weather.xctherm_cursor.layer >= region.layer_count)
      return;

    layer_index = weather.xctherm_cursor.layer;
  } else {
    const int li = FindActiveLayerIndex(settings);
    if (li < 0)
      return;

    layer_index = unsigned(li);
  }

  const char *label = gettext(region.layers[layer_index].short_label);
  if (label != nullptr && *label != '\0')
    text = label;
}

bool
ApplyCachedLayerOverlay(unsigned layer_index, unsigned utc_hour) noexcept
{
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  const auto &region = GetRegion(settings.model);
  if (layer_index >= region.layer_count)
    return false;

  auto &api = XCThermAPI::Instance();
  const auto &layer = region.layers[layer_index];
  if (MapShowsXCThermForecast(layer.api_parameter, utc_hour))
    return true;

#ifdef ENABLE_OPENGL
  XCThermGeoJSON::ForecastLayer cached_forecast;
  if (parsed_layer_cache.TakeIfMatches(layer.api_parameter, utc_hour,
                                       cached_forecast)) {
    ApplyForecastLayerToMap(std::move(cached_forecast),
                            gettext(layer.short_label),
                            layer.api_parameter, utc_hour);
    return true;
  }
#endif

  const std::string &cached =
    api.GetCachedGeoJSON(layer.api_parameter, utc_hour);
  if (cached.empty()) {
    LogFmt("xctherm: cache miss {}@{}h", layer.api_parameter, utc_hour);
    return false;
  }

  ApplyForecastToMap(cached, gettext(layer.short_label),
                       layer.api_parameter, utc_hour);
  return true;
}

bool
ApplyCachedLayerOverlayAuto(unsigned layer_index) noexcept
{
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  const auto &region = GetRegion(settings.model);
  if (layer_index >= region.layer_count)
    return false;

  const auto &layer = region.layers[layer_index];
  const auto hours =
    XCThermAPI::Instance().GetCachedHours(layer.api_parameter);
  if (hours.empty())
    return false;

  const auto utc = GetUtcTimeParts();
  const int idx = PickAutoTimeIndex(hours, utc.hour, utc.minute);
  const unsigned hour = idx >= 0 ? hours[unsigned(idx)] : hours[0];
  return ApplyCachedLayerOverlay(layer_index, hour);
}

void
RestoreActiveLayerOverlay() noexcept
{
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  const int layer_index = ResolveDisplayLayerIndex(settings);
  if (layer_index < 0)
    return;

  if (!ApplyCachedLayerOverlayAuto(unsigned(layer_index)))
    ClearMapOverlay();
}

void
ApplyJobPreviewToMap(const std::shared_ptr<XCThermDownloadJob> &job) noexcept
{
  if (job == nullptr || job->cancel.load() ||
      job->error_eptr || job->succeeded_or_cached.load() == 0)
    return;

  std::lock_guard lock{job->result_mutex};
  if (!job->first_forecast.IsEmpty()) {
    /* The span starts at the current UTC hour; when the job has not
       reported the first slot's UTC yet, fall back to current_utc. */
    const unsigned shown_utc = job->has_first_forecast_utc
      ? job->first_forecast_utc
      : job->current_utc;
    ApplyForecastLayerToMap(std::move(job->first_forecast),
                            gettext(job->target_label.c_str()),
                            job->param.c_str(), shown_utc);
  } else {
    RestoreActiveLayerOverlay();
  }
}

std::shared_ptr<XCThermDownloadJob>
StartSpanDownload(
  const XCThermSettings &settings, unsigned layer_index,
  std::function<void(std::shared_ptr<XCThermDownloadJob>)> on_finished)
{
  if (settings.download_span_hours == 0 || !settings.credentials.IsDefined())
    return nullptr;

  if (!XCThermAPI::Instance().EnsureAuthenticated()) {
    LogFmt("xctherm: cannot start download — auth failed");
    return nullptr;
  }

  auto *glue = GetXCThermDownloadGlue();
  if (glue == nullptr || Net::curl == nullptr || glue->IsRunning())
    return nullptr;

  auto job = MakeXCThermSpanJob(settings, layer_index, GetUtcTimeParts().hour);
  if (job == nullptr)
    return nullptr;

  auto shared = job;
  glue->Start(std::move(job), std::move(on_finished));
  return shared;
}

void
RequestBackgroundIndexFetch(std::function<void()> on_ready)
{
  auto *glue = GetXCThermDownloadGlue();
  if (glue == nullptr)
    return;

  if (XCThermAPI::Instance().IsIndexLoaded()) {
    if (on_ready)
      on_ready();
    return;
  }

  glue->StartIndexFetch(std::move(on_ready));
}

void
MaybeFetchActiveLayerSpan(
  std::function<void(std::shared_ptr<XCThermDownloadJob>)> on_finished)
{
  if (!XCThermAPI::Instance().IsIndexLoaded())
    return;

  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  const int active = FindActiveLayerIndex(settings);
  if (active < 0)
    return;

  const auto &region = GetRegion(settings.model);
  const std::string &param = region.layers[unsigned(active)].api_parameter;
  if (!XCThermAPI::Instance().GetCachedHours(param).empty())
    return;

  if (auto_fetch_attempted_param == param)
    return;

  auto_fetch_attempted_param = param;
  LogFmt("xctherm: auto-fetching active layer {}", param);

  if (!StartSpanDownload(settings, unsigned(active),
                         [finished = std::move(on_finished)](
                           std::shared_ptr<XCThermDownloadJob> done) mutable {
                           ApplyJobPreviewToMap(done);
                           if (finished)
                             finished(std::move(done));
                         }))
    auto_fetch_attempted_param.clear();
}

void
ResetAutoFetchAttempt() noexcept
{
  auto_fetch_attempted_param.clear();
}

void
ActivatePageOverlay() noexcept
{
  auto &weather = CommonInterface::SetUIState().weather;
  auto &session = weather.xctherm;
  auto &cursor = weather.xctherm_cursor;
  if (cursor.altitude_manual_override || cursor.time_manual_override)
    session.cursor_initialized = true;

  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;

  XCThermAPI::Instance().PrepareSession(settings);
  ApplyCursorOverlayFromSession();

  RequestBackgroundIndexFetch([]{
    MaybeFetchActiveLayerSpan(nullptr);
  });
}

} // namespace XCTherm
