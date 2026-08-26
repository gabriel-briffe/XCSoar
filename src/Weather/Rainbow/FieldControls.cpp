// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "FieldControls.hpp"
#include "RainbowOverlay.hpp"
#include "Interface.hpp"
#include "UIState.hpp"
#include "UIGlobals.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "Profile/Current.hpp"
#include "Profile/PageProfile.hpp"
#include "net/http/Init.hpp"
#include "net/State.hpp"
#include "lib/curl/Global.hxx"
#include "Operation/Operation.hpp"
#include "ui/event/CoInjectFunction.hpp"
#include "ui/event/Timer.hpp"
#include "util/StaticString.hxx"
#include "Language/Language.hpp"
#include "time/Convert.hxx"
#include "Geo/GeoBounds.hpp"
#include "Weather/MapOverlay/ControlsWidget.hpp"
#include "ActionInterface.hpp"
#include "LogFile.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>
#include <vector>

namespace Rainbow {

namespace {

std::unique_ptr<UI::CoInjectFunction<std::vector<PreparedTile>>> refresh_job;
std::unique_ptr<UI::CoInjectFunction<bool>> prefetch_job;
QuietOperationEnvironment refresh_env;
GeoBounds last_bounds = GeoBounds::Invalid();
OverlayPlan last_refresh_plan;
int64_t prefetch_reference = 0;
int64_t last_complete_live_epoch = 0;
std::chrono::steady_clock::time_point last_snapshot_poll{};
bool overlay_active = false;

/** True while Auto mode is waiting for the OS to report connectivity. */
bool waiting_for_connectivity = false;

void CancelAutoSchedule() noexcept;
void CancelPrefetchJob() noexcept;
void ScheduleAutoAfterSuccess() noexcept;
void ScheduleAutoAfterFailure() noexcept;
void ScheduleAutoConnectivityPoll() noexcept;
void UpdateAutoScheduleAfterAttempt(bool success) noexcept;
void StartRefresh(bool force_snapshot_poll = false) noexcept;

[[nodiscard]] int64_t AlignSnapshot(int64_t timestamp) noexcept;
[[nodiscard]] int64_t LatestSnapshotFloor() noexcept;
[[nodiscard]] bool SnapshotPollDue(bool force) noexcept;
void RecordSnapshotPollStart() noexcept;
[[nodiscard]] bool TryRestoreLiveFromCache(int64_t epoch) noexcept;

[[nodiscard]]
bool
IsAutoMode() noexcept
{
  return CommonInterface::GetUIState().weather.rainbow_cursor.time <= 0;
}

[[nodiscard]]
bool
IsDisconnected() noexcept
{
#ifdef HAVE_NET_STATE
  return GetNetState() == NetState::DISCONNECTED;
#else
  return false;
#endif
}

void
EnsureJob() noexcept
{
  if (refresh_job == nullptr && Net::curl != nullptr)
    refresh_job = std::make_unique<UI::CoInjectFunction<std::vector<PreparedTile>>>(
      Net::curl->GetEventLoop());
  if (prefetch_job == nullptr && Net::curl != nullptr)
    prefetch_job = std::make_unique<UI::CoInjectFunction<bool>>(
      Net::curl->GetEventLoop());
}

void
CancelPrefetchJob() noexcept
{
  prefetch_reference = 0;
  if (prefetch_job != nullptr)
    prefetch_job->Cancel();
}

void
StartHistoryPrefetch(int64_t reference) noexcept
{
  if (!overlay_active || !IsAutoMode() || reference <= 0 ||
      !last_refresh_plan.IsValid())
    return;

  EnsureJob();
  if (prefetch_job == nullptr || Net::curl == nullptr)
    return;

  CancelPrefetchJob();
  prefetch_reference = reference;

  const auto &settings =
    CommonInterface::GetComputerSettings().weather.rainbow;
  if (!settings.IsDefined())
    return;

  prefetch_job->Start(
    PrefetchHistorySnapshots(*Net::curl, settings.api_key.c_str(),
                             last_refresh_plan, reference, refresh_env),
    [](bool) {
      LogFmt("rainbow: history prefetch complete");
    },
    [](std::exception_ptr error) {
      try {
        if (error)
          std::rethrow_exception(error);
      } catch (const std::exception &e) {
        LogFmt("rainbow: history prefetch failed: {}", e.what());
      } catch (...) {
        LogFmt("rainbow: history prefetch failed");
      }
    });
}

void
StartRefresh(bool force_snapshot_poll) noexcept
{
#ifdef ENABLE_OPENGL
  /* Never install tiles unless ActivatePageOverlay() ran — pan
     suspend must not let Render() steal slots from SkySight/XCTherm. */
  if (!overlay_active)
    return;

  CancelPrefetchJob();

  EnsureJob();
  if (refresh_job == nullptr || Net::curl == nullptr)
    return;

  const auto &settings =
    CommonInterface::GetComputerSettings().weather.rainbow;
  if (!settings.IsDefined()) {
    LogFmt("rainbow: missing API key");
    return;
  }

  const auto &cursor =
    CommonInterface::GetUIState().weather.rainbow_cursor;
  if (!cursor.satellite && !cursor.rain)
    return;

  if (IsAutoMode() && IsDisconnected()) {
    LogFmt("rainbow: skip fetch (auto, disconnected)");
    if (overlay_active)
      ScheduleAutoConnectivityPoll();
    return;
  }

  auto *map = UIGlobals::GetMapIfActive();
  if (map == nullptr)
    return;

  int64_t plan_snapshot = cursor.time;
  if (IsAutoMode()) {
    const bool poll_due = SnapshotPollDue(force_snapshot_poll);
    if (!poll_due) {
      const int64_t cached_epoch = last_complete_live_epoch;
      if (cached_epoch > 0 && TryRestoreLiveFromCache(cached_epoch))
        return;

      if (cached_epoch > 0)
        plan_snapshot = cached_epoch;
      else
        return;
    } else {
      plan_snapshot = PageLayout::RAINBOW_TIME_AUTO;
      RecordSnapshotPollStart();
    }
  }

  /* Snapshot viewport on the UI thread; downloads run on the network
     thread and must not touch OpenGL Bitmaps / map overlays. */
  const auto plan = BuildOverlayPlan(*map, cursor.satellite, cursor.rain,
                                     plan_snapshot);
  if (!plan.IsValid())
    return;

  last_refresh_plan = plan;

  const unsigned expected_tiles = plan.CountPlannedTiles();

  refresh_job->Cancel();
  refresh_job->Start(
    DownloadOverlayTiles(*Net::curl, settings.api_key.c_str(),
                         plan, refresh_env),
    [expected_tiles](std::vector<PreparedTile> tiles) {
      /* Prefer rain's snapshot for the label when both layers are on;
         clouds and precip can publish different latest epochs. */
      int64_t sat_snap = 0;
      int64_t rain_snap = 0;
      for (const auto &tile : tiles) {
        if (tile.snapshot <= 0)
          continue;
        if (tile.layer_id == LayerId::RAIN)
          rain_snap = tile.snapshot;
        else
          sat_snap = tile.snapshot;
      }

      auto &cursor =
        CommonInterface::SetUIState().weather.rainbow_cursor;
      if (rain_snap > 0)
        cursor.displayed_time = rain_snap;
      else if (sat_snap > 0)
        cursor.displayed_time = sat_snap;

      const unsigned count = InstallPreparedOverlays(std::move(tiles));
      /* Partial viewport fills still show and update the bottom time,
         but Auto retries in 2 minutes instead of waiting for :x2. */
      const bool complete =
        expected_tiles > 0 && count >= expected_tiles;
      cursor.tiles_complete = complete;
      if (complete && IsAutoMode() && cursor.displayed_time > 0)
        last_complete_live_epoch =
          AlignSnapshot(cursor.displayed_time);
      LogFmt("rainbow: applied {}/{} tiles (snapshot {}, {})",
             count, expected_tiles, cursor.displayed_time,
             complete ? "complete" : "incomplete");
      WeatherMapOverlay::RefreshControlsLabels();
      ActionInterface::SendUIState(false);
      UpdateAutoScheduleAfterAttempt(complete);
      if (complete && IsAutoMode()) {
        const int64_t ref = cursor.displayed_time > 0
          ? AlignSnapshot(cursor.displayed_time)
          : LatestSnapshotFloor();
        StartHistoryPrefetch(ref);
      }
    },
    [](std::exception_ptr error) {
      try {
        if (error)
          std::rethrow_exception(error);
      } catch (const std::exception &e) {
        LogFmt("rainbow: refresh failed: {}", e.what());
      } catch (...) {
        LogFmt("rainbow: refresh failed");
      }
      WeatherMapOverlay::RefreshControlsLabels();
      UpdateAutoScheduleAfterAttempt(false);
    });
#endif
}

void
OnAutoRefreshTimer() noexcept
{
  if (!overlay_active || !IsAutoMode()) {
    CancelAutoSchedule();
    return;
  }

  if (IsDisconnected()) {
    ScheduleAutoConnectivityPoll();
    return;
  }

  /* Connectivity restored (or scheduled publish / retry window). */
  waiting_for_connectivity = false;
  StartRefresh(true);
}

UI::Timer auto_refresh_timer{[]() noexcept {
  OnAutoRefreshTimer();
}};

[[nodiscard]]
int64_t
AlignSnapshot(int64_t timestamp) noexcept
{
  if (timestamp <= 0)
    return 0;
  return (timestamp / SNAPSHOT_INTERVAL_SECONDS) * SNAPSHOT_INTERVAL_SECONDS;
}

[[nodiscard]]
int64_t
LatestSnapshotFloor() noexcept
{
  return AlignSnapshot(std::time(nullptr));
}

[[nodiscard]]
bool
SnapshotPollDue(bool force) noexcept
{
  if (force)
    return true;

  if (last_snapshot_poll == std::chrono::steady_clock::time_point{})
    return true;

  return std::chrono::steady_clock::now() - last_snapshot_poll >=
    std::chrono::seconds{SNAPSHOT_POLL_MIN_SECONDS};
}

void
RecordSnapshotPollStart() noexcept
{
  last_snapshot_poll = std::chrono::steady_clock::now();
}

[[nodiscard]]
bool
TryRestoreLiveFromCache(int64_t epoch) noexcept
{
  if (epoch <= 0 || !overlay_active)
    return false;

  auto *map = UIGlobals::GetMapIfActive();
  if (map == nullptr)
    return false;

  const auto &cursor =
    CommonInterface::GetUIState().weather.rainbow_cursor;
  auto plan = BuildOverlayPlan(*map, cursor.satellite, cursor.rain, epoch);
  if (!plan.IsValid())
    return false;

  last_refresh_plan = plan;

  const unsigned expected_tiles = plan.CountPlannedTiles();
  const unsigned count = InstallCachedOverlayTiles(plan, epoch);
  if (expected_tiles == 0 || count < expected_tiles)
    return false;

  auto &mutable_cursor =
    CommonInterface::SetUIState().weather.rainbow_cursor;
  mutable_cursor.displayed_time = epoch;
  mutable_cursor.tiles_complete = true;
  last_complete_live_epoch = epoch;

  LogFmt("rainbow: restored live cache (snapshot {})", epoch);
  WeatherMapOverlay::RefreshControlsLabels();
  ActionInterface::SendUIState(false);
  UpdateAutoScheduleAfterAttempt(true);
  return true;
}

/**
 * Seconds until the next wall-clock ``:x2`` after a 10-minute boundary
 * (e.g. 12:42, 21:02), when a new product is expected to be published.
 */
[[nodiscard]]
std::chrono::steady_clock::duration
DurationUntilNextPublishWindow() noexcept
{
  const time_t now = std::time(nullptr);
  const time_t boundary =
    (now / SNAPSHOT_INTERVAL_SECONDS) * SNAPSHOT_INTERVAL_SECONDS;
  time_t candidate = boundary + SNAPSHOT_PUBLISH_LAG_SECONDS;
  if (candidate <= now)
    candidate = boundary + SNAPSHOT_INTERVAL_SECONDS +
      SNAPSHOT_PUBLISH_LAG_SECONDS;

  const auto seconds = std::max<time_t>(1, candidate - now);
  return std::chrono::seconds{seconds};
}

void
CancelAutoSchedule() noexcept
{
  waiting_for_connectivity = false;
  auto_refresh_timer.Cancel();
}

void
ScheduleAutoConnectivityPoll() noexcept
{
  if (!overlay_active || !IsAutoMode()) {
    CancelAutoSchedule();
    return;
  }

  waiting_for_connectivity = true;
  auto_refresh_timer.Schedule(
    std::chrono::seconds{AUTO_CONNECTIVITY_POLL_SECONDS});
  LogFmt("rainbow: auto waiting for connectivity (poll {}s)",
         AUTO_CONNECTIVITY_POLL_SECONDS);
}

void
ScheduleAutoAfterSuccess() noexcept
{
  if (!overlay_active || !IsAutoMode()) {
    CancelAutoSchedule();
    return;
  }

  if (IsDisconnected()) {
    ScheduleAutoConnectivityPoll();
    return;
  }

  waiting_for_connectivity = false;
  const auto delay = DurationUntilNextPublishWindow();
  auto_refresh_timer.Schedule(delay);
  LogFmt("rainbow: auto next refresh in {}s (:x2 window)",
         std::chrono::duration_cast<std::chrono::seconds>(delay).count());
}

void
ScheduleAutoAfterFailure() noexcept
{
  if (!overlay_active || !IsAutoMode()) {
    CancelAutoSchedule();
    return;
  }

  if (IsDisconnected()) {
    ScheduleAutoConnectivityPoll();
    return;
  }

  waiting_for_connectivity = false;
  auto_refresh_timer.Schedule(
    std::chrono::seconds{AUTO_FAIL_RETRY_SECONDS});
  LogFmt("rainbow: auto retry in {}s after failure",
         AUTO_FAIL_RETRY_SECONDS);
}

void
UpdateAutoScheduleAfterAttempt(bool success) noexcept
{
  if (!overlay_active || !IsAutoMode()) {
    CancelAutoSchedule();
    return;
  }

  if (success)
    ScheduleAutoAfterSuccess();
  else
    ScheduleAutoAfterFailure();
}

} // namespace

void
ApplyCursorFromPageLayout(const PageLayout &layout) noexcept
{
  auto &cursor = CommonInterface::SetUIState().weather.rainbow_cursor;
  cursor.time = layout.rainbow_time;
  cursor.satellite = layout.rainbow_satellite;
  cursor.rain = layout.rainbow_rain;
}

void
PersistCursorToPage() noexcept
{
  auto &settings = CommonInterface::SetUISettings().pages;
  const unsigned index = CommonInterface::GetUIState().pages.current_index;
  if (index >= settings.n_pages)
    return;

  auto &page = settings.pages[index];
  if (!page.UsesRainbowOverlay())
    return;

  const auto &cursor = CommonInterface::GetUIState().weather.rainbow_cursor;
  page.rainbow_time = cursor.time;
  page.rainbow_satellite = cursor.satellite;
  page.rainbow_rain = cursor.rain;
  page.Normalise();
  Profile::Save(Profile::map, settings);
}

void
ClearMapOverlay() noexcept
{
  /* Do not wipe foreign overlays (SkySight tiles, XCTherm GeoJSON)
     when Rainbow never activated a page overlay. */
  if (!overlay_active)
    return;

  overlay_active = false;
  last_bounds = GeoBounds::Invalid();
  CancelAutoSchedule();
  CancelHistoryPrefetch();
  auto &cursor = CommonInterface::SetUIState().weather.rainbow_cursor;
  cursor.displayed_time = 0;
  cursor.tiles_complete = false;
  if (refresh_job != nullptr)
    refresh_job->Cancel();
  ClearOverlays();
}

void
ActivatePageOverlay() noexcept
{
  overlay_active = true;
  StartRefresh();
}

void
Render() noexcept
{
  /* Gate on overlay_active only — pan-suspend of other providers must
     never start a Rainbow fetch or paint. */
  if (!overlay_active)
    return;

#ifdef ENABLE_OPENGL
  auto *map = UIGlobals::GetMapIfActive();
  if (map == nullptr)
    return;

  const auto bounds = map->VisibleProjection().GetScreenBounds();
  if (!bounds.Check() || !bounds.IsValid())
    return;

  if (last_bounds.IsValid()) {
    const auto moved =
      last_bounds.GetCenter().DistanceS(bounds.GetCenter());
    const auto scale =
      bounds.GetNorthWest().DistanceS(bounds.GetNorthEast());
    const auto last_scale =
      last_bounds.GetNorthWest().DistanceS(last_bounds.GetNorthEast());
    if (moved < scale * 0.25 &&
        std::abs(scale - last_scale) < scale * 0.2)
      return;
  }

  last_bounds = bounds;
  StartRefresh();
#endif
}

bool
StepTime(int delta) noexcept
{
  auto &cursor = CommonInterface::SetUIState().weather.rainbow_cursor;
  const int64_t latest = LatestSnapshotFloor();
  int64_t current = cursor.time > 0 ? AlignSnapshot(cursor.time) : latest;
  current += int64_t(delta) * SNAPSHOT_INTERVAL_SECONDS;

  const int64_t oldest = latest - SNAPSHOT_HISTORY_SECONDS;
  if (current > latest)
    current = latest;
  if (current < oldest)
    current = oldest;

  if (current == latest)
    cursor.time = PageLayout::RAINBOW_TIME_AUTO;
  else
    cursor.time = current;

  cursor.displayed_time = 0;
  cursor.tiles_complete = false;
  CommonInterface::SetUIState().weather.rainbow.cursor_initialized =
    cursor.time != PageLayout::RAINBOW_TIME_AUTO;

  if (!IsAutoMode())
    CancelAutoSchedule();

  PersistCursorToPage();
  StartRefresh();
  return true;
}

namespace {

[[nodiscard]]
unsigned
LayerMode(bool satellite, bool rain) noexcept
{
  if (satellite && rain)
    return 2;
  if (rain)
    return 1;
  return 0; /* satellite only (default) */
}

void
ApplyLayerMode(unsigned mode) noexcept
{
  auto &cursor = CommonInterface::SetUIState().weather.rainbow_cursor;
  switch (mode % 3) {
  case 1:
    cursor.satellite = false;
    cursor.rain = true;
    break;
  case 2:
    cursor.satellite = true;
    cursor.rain = true;
    break;
  default:
    cursor.satellite = true;
    cursor.rain = false;
    break;
  }
}

} // namespace

bool
StepLayer(int delta) noexcept
{
  if (delta == 0)
    return false;

  auto &cursor = CommonInterface::SetUIState().weather.rainbow_cursor;
  const unsigned current = LayerMode(cursor.satellite, cursor.rain);
  const unsigned next = (current + (delta > 0 ? 1u : 2u)) % 3u;
  if (next == current)
    return false;

  ApplyLayerMode(next);
  PersistCursorToPage();
  StartRefresh();
  return true;
}

void
FormatTimeLabel(StaticString<64> &text) noexcept
{
  const auto &cursor = CommonInterface::GetUIState().weather.rainbow_cursor;
  const bool automatic = cursor.time <= 0;

  /* Manual: show the requested slot.  Auto: show the snapshot on the map
     (do not reuse a manual replay epoch after returning to Auto). */
  int64_t stamp = automatic ? cursor.displayed_time : cursor.time;
  if (stamp <= 0) {
    text = automatic ? C_("Status", "Auto")
                     : C_("Status", "Live");
    return;
  }

  const auto tm = GmTime(std::chrono::system_clock::from_time_t(time_t(stamp)));
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%H:%M UTC", &tm);

  if (automatic)
    text.Format("%s %s", C_("Status", "Auto"), buffer);
  else
    text = buffer;
}

void
FormatLayerLabel(StaticString<64> &text) noexcept
{
  const auto &cursor = CommonInterface::GetUIState().weather.rainbow_cursor;
  text.clear();
  if (cursor.satellite)
    text = C_("Weather layer", "Sat");
  if (cursor.rain) {
    if (!text.empty())
      text.append(" ");
    text.append(C_("Weather layer", "Rain"));
  }
  if (text.empty())
    text = C_("Weather layer", "Sat");
}

bool
HasActiveOverlay() noexcept
{
  /* Controls stay available once ActivatePageOverlay() ran — do not
     wait for the first tile download, and do not treat pan-suspend
     of an inactive Rainbow session as ownership. */
  return overlay_active;
}

void
DiscardInactiveOverlays() noexcept
{
  if (overlay_active)
    return;

  ClearOverlays();
}

bool
GetTimeAutoAdvance() noexcept
{
  return IsAutoMode();
}

void
SetTimeAutoAdvance(bool auto_advance, int64_t known_live_epoch) noexcept
{
  auto &cursor = CommonInterface::SetUIState().weather.rainbow_cursor;
  if (auto_advance) {
    if (IsAutoMode() && known_live_epoch <= 0)
      return;

    cursor.time = PageLayout::RAINBOW_TIME_AUTO;

    const int64_t restore = known_live_epoch > 0
      ? AlignSnapshot(known_live_epoch)
      : last_complete_live_epoch;
    const bool restored = TryRestoreLiveFromCache(restore);
    if (!restored) {
      cursor.displayed_time = 0;
      cursor.tiles_complete = false;
    }
  } else if (IsAutoMode()) {
    cursor.time = LatestSnapshotFloor();
    cursor.displayed_time = 0;
    cursor.tiles_complete = false;
  } else {
    return;
  }

  CommonInterface::SetUIState().weather.rainbow.cursor_initialized =
    !auto_advance;

  CancelAutoSchedule();
  CancelPrefetchJob();
  PersistCursorToPage();

  if (auto_advance) {
    if (cursor.displayed_time > 0)
      return;

    StartRefresh(false);
  } else {
    StartRefresh(false);
  }
}

int64_t
GetLiveReferenceTime() noexcept
{
  const auto &cursor = CommonInterface::GetUIState().weather.rainbow_cursor;
  if (cursor.displayed_time > 0)
    return AlignSnapshot(cursor.displayed_time);

  return LatestSnapshotFloor();
}

bool
SetPageTime(int64_t time) noexcept
{
  auto &cursor = CommonInterface::SetUIState().weather.rainbow_cursor;
  if (cursor.time == time)
    return false;

  cursor.time = time;
  cursor.displayed_time = 0;
  cursor.tiles_complete = false;
  CommonInterface::SetUIState().weather.rainbow.cursor_initialized =
    cursor.time != PageLayout::RAINBOW_TIME_AUTO;

  CancelAutoSchedule();
  CancelPrefetchJob();
  PersistCursorToPage();
  StartRefresh();
  return true;
}

void
CancelHistoryPrefetch() noexcept
{
  CancelPrefetchJob();
}

bool
CanReplayLiveTime() noexcept
{
  if (!IsAutoMode() || !overlay_active)
    return false;

  const int64_t reference = GetLiveReferenceTime();
  if (reference <= 0)
    return false;

  const int64_t oldest = reference - SNAPSHOT_HISTORY_SECONDS;
  return reference - 2 * SNAPSHOT_INTERVAL_SECONDS >= oldest;
}

} // namespace Rainbow
