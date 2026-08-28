// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "IgcMetaCache.hpp"

#include "IGC/FlightEvents.hpp"
#include "IGC/IGCParser.hpp"
#include "Formatter/TimeFormatter.hpp"
#include "io/FileLineReader.hpp"
#include "ui/event/Notify.hpp"
#include "co/InvokeTask.hxx"
#include "io/async/AsioThread.hpp"
#include "io/async/GlobalAsioThread.hpp"
#include "util/BindMethod.hxx"
#include "util/StringAPI.hxx"

#include <chrono>
#include <cstring>
#include <utility>

/**
 * Lightweight B-record parser that extracts only the time and GPS
 * validity flag, skipping the expensive location, altitude, and
 * extension parsing that IGCParseFix() performs.
 */
static bool
ParseBRecordTime(const char *line, BrokenTime &time,
                 bool &gps_valid) noexcept
{
  if (line[0] != 'B')
    return false;

  /* time is at offset 1..6, validity char at offset 24 */
  if (std::strlen(line) < 25)
    return false;

  if (!IGCParseTime(line + 1, time))
    return false;

  if (line[24] == 'A')
    gps_valid = true;
  else if (line[24] == 'V')
    gps_valid = false;
  else
    return false;

  return true;
}

/**
 * Parse XCSoar takeoff/landing E-records: EHHMMSS + event code.
 */
[[gnu::pure]]
static bool
ParseFlightEventTime(const char *line, BrokenTime &time) noexcept
{
  if (line[0] != 'E')
    return false;

  if (std::strlen(line) < 7)
    return false;

  return IGCParseTime(line + 1, time);
}

IgcMetaCache::~IgcMetaCache() noexcept
{
  Shutdown();
}

IgcMetaCache::CacheEntry
IgcMetaCache::ParseEntry(Path path) noexcept
{
  CacheEntry entry;
  entry.path = path;

  BrokenTime takeoff_utc = BrokenTime::Invalid();
  BrokenTime landing_utc = BrokenTime::Invalid();
  bool has_takeoff = false;
  bool has_landing = false;

  BrokenTime first_b = BrokenTime::Invalid();
  BrokenTime last_b = BrokenTime::Invalid();
  bool has_first_b = false;
  bool has_last_b = false;

  try {
    FileLineReaderA reader(path);
    char *line;
    while ((line = reader.ReadLine()) != nullptr) {
      BrokenTime time;
      if (ParseFlightEventTime(line, time)) {
        if (StringIsEqual(line + 7, IGCFlightEvent::TAKEOFF)) {
          if (!has_takeoff) {
            takeoff_utc = time;
            has_takeoff = true;
          }
        } else if (StringIsEqual(line + 7, IGCFlightEvent::LANDING)) {
          landing_utc = time;
          has_landing = true;
        }
        continue;
      }

      bool gps_valid;
      if (ParseBRecordTime(line, time, gps_valid) && gps_valid) {
        if (!has_first_b) {
          first_b = time;
          has_first_b = true;
        }
        last_b = time;
        has_last_b = true;
      }
    }
  } catch (...) {
    // ignore parse errors
  }

  /* Prefer XCSoar TKOFF/LAND E-records (same as the Logbook method).
     Fall back to first/last valid B-records for older IGC files. */
  if (has_takeoff && has_landing) {
    entry.meta.start = takeoff_utc;
    entry.meta.end = landing_utc;
    entry.meta.has_start = true;
    entry.meta.has_end = true;
  } else if (has_first_b && has_last_b) {
    entry.meta.start = first_b;
    entry.meta.end = last_b;
    entry.meta.has_start = true;
    entry.meta.has_end = true;
  }

  entry.text = "";

  if (entry.meta.has_start && entry.meta.has_end) {
    StaticString<32> lbuf;
    lbuf.Format("%02u:%02u - %02u:%02u",
                (unsigned)entry.meta.start.hour,
                (unsigned)entry.meta.start.minute,
                (unsigned)entry.meta.end.hour,
                (unsigned)entry.meta.end.minute);
    entry.text = lbuf.c_str();

    /* Duration from displayed HH:MM only (ignore seconds) so it
       matches paper logbook arithmetic: landing − takeoff. */
    int64_t s_min = (int64_t)entry.meta.start.hour * 60
      + (int64_t)entry.meta.start.minute;
    int64_t e_min = (int64_t)entry.meta.end.hour * 60
      + (int64_t)entry.meta.end.minute;
    int64_t diff_min = e_min - s_min;
    if (diff_min < 0)
      diff_min += 24 * 60;
    auto dur = FormatTimespanSmart(std::chrono::seconds(diff_min * 60), 2);
    entry.text.append(" (");
    entry.text.append(dur.c_str());
    entry.text.append(")");
  }

  return entry;
}

IgcMetaCache::CacheEntry *
IgcMetaCache::FindOrParse(Path path) noexcept
{
  {
    const std::lock_guard lock{cache_mutex};
    for (auto &e : cache) {
      if (e.path == path)
        return &e;
    }
  }

  CacheEntry entry = ParseEntry(path);

  const std::lock_guard lock{cache_mutex};
  for (auto &e : cache) {
    if (e.path == path)
      return &e;
  }

  cache.push_back(std::move(entry));
  return &cache.back();
}

std::string
IgcMetaCache::GetCompactInfo(Path path) noexcept
{
  CacheEntry *entry = FindOrParse(path);
  return entry != nullptr ? std::string(entry->text.c_str()) : std::string();
}

const char *
IgcMetaCache::GetCompactInfoPtr(Path path) noexcept
{
  CacheEntry *entry = FindOrParse(path);
  return entry != nullptr ? entry->text.c_str() : nullptr;
}

Co::InvokeTask
IgcMetaCache::FillCacheCoro(std::vector<AllocatedPath> paths) noexcept
{
  for (const auto &path : paths) {
    GetCompactInfo(Path(path.c_str()));
  }

  co_return;
}

void
IgcMetaCache::OnFillComplete([[maybe_unused]] std::exception_ptr error) noexcept
{
  // Notify UI that fill is complete (ignore any errors)
  if (auto *notify = current_notify.exchange(nullptr))
    notify->SendNotification();
}

void
IgcMetaCache::StartBackgroundFill(std::vector<AllocatedPath> paths,
                                  UI::Notify *notify) noexcept
{
  if (!inject_task)
    inject_task = std::make_unique<Co::InjectTask>(asio_thread->GetEventLoop());

  if (*inject_task) {
    CancelBackgroundFill();
    inject_task.reset();
    inject_task = std::make_unique<Co::InjectTask>(asio_thread->GetEventLoop());
  }

  current_notify.store(notify);
  inject_task->Start(FillCacheCoro(std::move(paths)), BIND_THIS_METHOD(OnFillComplete));
}

void
IgcMetaCache::CancelBackgroundFill() noexcept
{
  if (!inject_task)
    return;

  current_notify.store(nullptr);
  inject_task->Cancel();
}

void
IgcMetaCache::Shutdown() noexcept
{
  CancelBackgroundFill();
  inject_task.reset();
}

void
IgcMetaCache::PollBackgroundFill() noexcept
{
  if (!inject_task || !*inject_task)
    return;

  // No synchronous wait available; completion is reported via OnFillComplete().
}
