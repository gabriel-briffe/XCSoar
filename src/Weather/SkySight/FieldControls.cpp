// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "FieldControls.hpp"

#include "DataGlobals.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "PageSettings.hpp"
#include "UIState.hpp"
#include "Weather/MapOverlay/ControlsWidget.hpp"
#include "Weather/MapOverlay/PageCursor.hpp"
#include "Weather/SkySight/ForecastFormatter.hpp"
#include "Weather/SkySight/ForecastUtils.hpp"
#include "Weather/SkySight/LiveTileUtils.hpp"
#include "Weather/SkySight/SkySightClient.hpp"
#include "time/Convert.hxx"

#include <algorithm>
#include <ctime>
#include <limits>
#include <vector>

namespace SkySight {

static constexpr unsigned TIME_PICKER_AUTO =
  std::numeric_limits<unsigned>::max();

/** How far back live satellite/rain steps may go (matches Rainbow). */
static constexpr time_t LIVE_HISTORY_SECONDS = 2 * 3600;

[[nodiscard]] static RoughTimeDelta
DisplayOffsetFor(const SkySightClient &skysight, time_t utc_time) noexcept
{
  return skysight.GetForecastDisplayOffset(utc_time);
}

[[nodiscard]] static time_t
AlignLiveTime(time_t t) noexcept
{
  if (t <= 0)
    return 0;
  return (t / LIVE_TILE_INTERVAL_SECONDS) * LIVE_TILE_INTERVAL_SECONDS;
}

[[nodiscard]] static time_t
LatestLiveFloor(const Layer &layer) noexcept
{
  if (layer.HasKnownLiveTimestamp())
    return AlignLiveTime(layer.last_update);
  return AlignLiveTime(std::time(nullptr));
}

[[nodiscard]] static std::vector<time_t>
GetSelectableLiveTimes(const Layer &layer) noexcept
{
  const time_t latest = LatestLiveFloor(layer);
  std::vector<time_t> times;
  if (latest <= 0)
    return times;

  const time_t oldest = latest - LIVE_HISTORY_SECONDS;
  for (time_t t = latest; t >= oldest; t -= LIVE_TILE_INTERVAL_SECONDS)
    times.push_back(t);
  return times;
}

static void
FormatLiveUtcLabel(StaticString<64> &text, time_t utc) noexcept
{
  const auto tm = GmTime(std::chrono::system_clock::from_time_t(utc));
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%H:%M UTC", &tm);
  text = buffer;
}

[[nodiscard]] static const Layer *
GetLayer(const PageLayout &page) noexcept
{
  const auto skysight = DataGlobals::GetSkySight();
  return skysight != nullptr && page.UsesSkySightOverlay()
    ? skysight->GetSelectedLayer(page.skysight_overlay.c_str())
    : nullptr;
}

[[nodiscard]] static const PageLayout *
GetCurrentPage() noexcept
{
  const auto &pages = CommonInterface::GetUIState().pages;
  const auto &settings = CommonInterface::GetUISettings().pages;
  return pages.current_index < settings.n_pages
    ? &settings.pages[pages.current_index]
    : nullptr;
}

[[nodiscard]] static int
FindForecastIndex(const Layer &layer, const std::vector<time_t> &times,
                  int64_t selected_time) noexcept
{
  for (unsigned i = 0; i < times.size(); ++i)
    if (int64_t(times[i]) == selected_time ||
        (IsFullDayForecastLayer(layer) && selected_time > 0 &&
         IsSameForecastDay(times[i], time_t(selected_time))))
      return i;

  return -1;
}

void
ApplyCursorFromPageLayout(const PageLayout &page) noexcept
{
  if (const auto skysight = DataGlobals::GetSkySight(); skysight != nullptr)
    skysight->ApplyPageOverlay(page);
}

bool
EditTimeOnLayout(PageLayout &page) noexcept
{
  try {
    const auto skysight = DataGlobals::GetSkySight();
    const auto *layer = GetLayer(page);
    if (skysight == nullptr || layer == nullptr)
      return false;

    if (layer->SupportsLiveTiles()) {
      const auto times = GetSelectableLiveTimes(*layer);
      if (times.empty())
        return false;

      DataFieldEnum picker;
      picker.addEnumText(C_("Weather control", "Auto"), TIME_PICKER_AUTO);
      for (unsigned i = 0; i < times.size(); ++i) {
        StaticString<64> label;
        FormatLiveUtcLabel(label, times[i]);
        picker.addEnumText(label.c_str(), i);
      }

      if (page.skysight_time == PageLayout::SKYSIGHT_TIME_AUTO)
        picker.SetValue(TIME_PICKER_AUTO);
      else {
        const time_t selected = AlignLiveTime(time_t(page.skysight_time));
        unsigned current = 0;
        for (unsigned i = 0; i < times.size(); ++i)
          if (times[i] == selected) {
            current = i;
            break;
          }
        picker.SetValue(current);
      }

      if (!ComboPicker(_("SkySight Time"), picker, nullptr))
        return false;

      const unsigned selected = picker.GetValue();
      page.skysight_time = selected == TIME_PICKER_AUTO
        ? PageLayout::SKYSIGHT_TIME_AUTO
        : selected < times.size()
          ? int64_t(times[selected])
          : page.skysight_time;
      return selected == TIME_PICKER_AUTO || selected < times.size();
    }

    const auto times = GetSelectableForecastTimes(*layer);
    if (times.empty())
      return false;

    DataFieldEnum picker;
    picker.addEnumText(C_("Weather control", "Auto"), TIME_PICKER_AUTO);
    for (unsigned i = 0; i < times.size(); ++i)
      picker.addEnumText(FormatForecastTimeLabel(
        *layer, times[i],
        DisplayOffsetFor(*skysight, times[i])).c_str(), i);

    if (page.skysight_time == PageLayout::SKYSIGHT_TIME_AUTO)
      picker.SetValue(TIME_PICKER_AUTO);
    else {
      const int current = FindForecastIndex(*layer, times,
                                            page.skysight_time);
      picker.SetValue(current >= 0 ? unsigned(current) : 0);
    }

    if (!ComboPicker(_("SkySight Time"), picker, nullptr))
      return false;

    const unsigned selected = picker.GetValue();
    page.skysight_time = selected == TIME_PICKER_AUTO
      ? PageLayout::SKYSIGHT_TIME_AUTO
      : selected < times.size()
        ? int64_t(times[selected])
        : page.skysight_time;
    return selected == TIME_PICKER_AUTO || selected < times.size();
  } catch (...) {
    return false;
  }
}

LayerPickerResult
EditLayerOnLayout(PageLayout &page, bool offer_setup) noexcept
{
  const auto skysight = DataGlobals::GetSkySight();
  if (skysight == nullptr || skysight->NumSelectedLayers() == 0)
    return offer_setup ? LayerPickerResult::OPEN_SETUP
                       : LayerPickerResult::NONE;

  DataFieldEnum picker;
  unsigned current = 0;
  for (std::size_t i = 0; i < skysight->NumSelectedLayers(); ++i) {
    const auto *layer = skysight->GetSelectedLayer(i);
    if (layer == nullptr)
      continue;

    picker.addEnumText(layer->name.c_str(), int(i));
    if (layer->id == page.skysight_overlay.c_str())
      current = unsigned(i);
  }

  picker.SetValue(current);
  bool setup = false;
  if (!ComboPicker(_("SkySight Layer"), picker, nullptr,
                   offer_setup ? C_("Button", "Setup") : nullptr, &setup))
    return setup ? LayerPickerResult::OPEN_SETUP
                 : LayerPickerResult::NONE;

  const unsigned selected = picker.GetValue();
  const auto *layer = selected < skysight->NumSelectedLayers()
    ? skysight->GetSelectedLayer(selected)
    : nullptr;
  if (layer == nullptr)
    return LayerPickerResult::NONE;

  if (page.skysight_overlay != layer->id.c_str())
    page.skysight_overlay = layer->id;

  return LayerPickerResult::CHANGED;
}

static void
AfterLiveChange(const PageLayout &page) noexcept
{
  ApplyCursorFromPageLayout(page);
  CommonInterface::SetUIState().weather.skysight.cursor_initialized =
    page.skysight_time != PageLayout::SKYSIGHT_TIME_AUTO;
  WeatherMapOverlay::NotifyLiveCursorChange();
}

void
OpenTimePicker() noexcept
{
  const unsigned page_index =
    CommonInterface::GetUIState().pages.current_index;
  if (!WeatherMapOverlay::MutateOverlayPage(
        page_index, PageLayout::Overlay::SKYSIGHT,
        [](PageLayout &page) noexcept { return EditTimeOnLayout(page); }))
    return;

  AfterLiveChange(CommonInterface::GetUISettings().pages.pages[page_index]);
}

LayerPickerResult
OpenLayerPicker(bool offer_setup) noexcept
{
  LayerPickerResult result = LayerPickerResult::NONE;
  const unsigned page_index =
    CommonInterface::GetUIState().pages.current_index;
  if (!WeatherMapOverlay::MutateOverlayPage(
        page_index, PageLayout::Overlay::SKYSIGHT,
        [offer_setup, &result](PageLayout &page) noexcept {
          result = EditLayerOnLayout(page, offer_setup);
          return result == LayerPickerResult::CHANGED;
        }))
    return result;

  AfterLiveChange(CommonInterface::GetUISettings().pages.pages[page_index]);
  return result;
}

void
FormatTimeLabelForPage(StaticString<64> &text,
                       const PageLayout &page) noexcept
{
  const auto skysight = DataGlobals::GetSkySight();
  const auto *layer = GetLayer(page);
  if (layer == nullptr) {
    text = C_("Status", "No page layer");
    return;
  }

  if (layer->SupportsLiveTiles()) {
    const auto &cursor =
      CommonInterface::GetUIState().weather.skysight_cursor;
    const bool automatic =
      page.skysight_time == PageLayout::SKYSIGHT_TIME_AUTO;

    /* Prefer the map cursor (same source as the page title).  Do not invent
       a wall-clock / layer-floor time for Auto — Sat+Rain has no last_update
       and that fallback desyncs from the tiles. */
    int64_t stamp = cursor.displayed_time;
    if (stamp <= 0 && !automatic)
      stamp = page.skysight_time;

    if (stamp <= 0) {
      text = automatic ? C_("Status", "Auto") : C_("Status", "Live");
      return;
    }

    StaticString<64> label;
    FormatLiveUtcLabel(label, time_t(stamp));
    if (automatic)
      text.Format("%s %s", C_("Status", "Auto"), label.c_str());
    else
      text = label;
    return;
  }

  const bool automatic =
    page.skysight_time == PageLayout::SKYSIGHT_TIME_AUTO;
  const time_t timestamp = automatic
    ? layer->forecast_time
    : time_t(page.skysight_time);
  if (timestamp <= 0) {
    text = automatic ? C_("Status", "Auto") : C_("Status", "No data");
    return;
  }

  const auto label = FormatForecastTimeLabel(
    *layer, timestamp,
    skysight != nullptr
      ? DisplayOffsetFor(*skysight, timestamp)
      : RoughTimeDelta{});
  if (automatic)
    text.Format(_("AUTO: %s"), label.c_str());
  else if (layer->FindDatafile(timestamp) == nullptr)
    text.Format(_("%s [no data]"), label.c_str());
  else
    text = label;
}

void
FormatLayerLabelForPage(StaticString<64> &text,
                        const PageLayout &page) noexcept
{
  const auto *layer = GetLayer(page);
  text = layer != nullptr
    ? layer->name.c_str()
    : page.skysight_overlay.empty()
      ? C_("Status", "No page layer")
      : page.skysight_overlay.c_str();
}

bool
IsTimeSelectable(const PageLayout &page) noexcept
{
  const auto *layer = GetLayer(page);
  if (layer == nullptr)
    return false;
  if (layer->SupportsLiveTiles())
    return !GetSelectableLiveTimes(*layer).empty();
  return !GetSelectableForecastTimes(*layer).empty();
}

bool
IsTimeSelectable() noexcept
{
  const auto *page = GetCurrentPage();
  return page != nullptr && IsTimeSelectable(*page);
}

bool
HasSelectedLayer() noexcept
{
  const auto *page = GetCurrentPage();
  return page != nullptr && GetLayer(*page) != nullptr;
}

bool
HasSelectedTimeData() noexcept
{
  const auto *page = GetCurrentPage();
  if (page == nullptr)
    return false;

  const auto *layer = GetLayer(*page);
  if (layer == nullptr)
    return false;
  if (layer->SupportsLiveTiles())
    return true;

  const time_t timestamp = page->skysight_time == 0
    ? layer->forecast_time
    : time_t(page->skysight_time);
  return timestamp > 0 && layer->FindDatafile(timestamp) != nullptr;
}

bool
StepTime(int delta) noexcept
{
  if (delta == 0)
    return false;

  const unsigned page_index =
    CommonInterface::GetUIState().pages.current_index;
  if (!WeatherMapOverlay::MutateOverlayPage(
        page_index, PageLayout::Overlay::SKYSIGHT,
        [delta](PageLayout &page) noexcept {
          const auto *layer = GetLayer(page);
          if (layer == nullptr)
            return false;

          if (layer->SupportsLiveTiles()) {
            const time_t latest = LatestLiveFloor(*layer);
            if (latest <= 0)
              return false;

            time_t current = page.skysight_time != 0
              ? AlignLiveTime(time_t(page.skysight_time))
              : latest;
            current += time_t(delta) * LIVE_TILE_INTERVAL_SECONDS;

            const time_t oldest = latest - LIVE_HISTORY_SECONDS;
            if (current > latest)
              current = latest;
            if (current < oldest)
              current = oldest;

            const int64_t next = current >= latest
              ? PageLayout::SKYSIGHT_TIME_AUTO
              : int64_t(current);
            if (page.skysight_time == next)
              return false;
            page.skysight_time = next;
            return true;
          }

          const auto times = GetSelectableForecastTimes(*layer);
          if (times.empty())
            return false;

          const int64_t reference = page.skysight_time != 0
            ? page.skysight_time
            : int64_t(layer->forecast_time);
          int index = FindForecastIndex(*layer, times, reference);
          if (index < 0)
            index = 0;
          index += delta;
          if (index < 0 || index >= int(times.size()))
            return false;

          page.skysight_time = int64_t(times[unsigned(index)]);
          return true;
        }))
    return false;

  AfterLiveChange(CommonInterface::GetUISettings().pages.pages[page_index]);
  return true;
}

bool
StepLayer(int delta) noexcept
{
  const auto skysight = DataGlobals::GetSkySight();
  if (skysight == nullptr || delta == 0 ||
      skysight->NumSelectedLayers() == 0)
    return false;

  const unsigned page_index =
    CommonInterface::GetUIState().pages.current_index;
  if (!WeatherMapOverlay::MutateOverlayPage(
        page_index, PageLayout::Overlay::SKYSIGHT,
        [skysight, delta](PageLayout &page) noexcept {
          int current = 0;
          for (std::size_t i = 0; i < skysight->NumSelectedLayers(); ++i)
            if (const auto *layer = skysight->GetSelectedLayer(i);
                layer != nullptr &&
                layer->id == page.skysight_overlay.c_str()) {
              current = int(i);
              break;
            }

          const int count = int(skysight->NumSelectedLayers());
          const int next = ((current + delta) % count + count) % count;
          const auto *layer = skysight->GetSelectedLayer(unsigned(next));
          if (layer == nullptr)
            return false;

          page.skysight_overlay = layer->id;
          return true;
        }))
    return false;

  AfterLiveChange(CommonInterface::GetUISettings().pages.pages[page_index]);
  return true;
}

bool
GetTimeAutoAdvance() noexcept
{
  const auto *page = GetCurrentPage();
  return page != nullptr && page->skysight_time ==
      PageLayout::SKYSIGHT_TIME_AUTO;
}

void
SetTimeAutoAdvance(bool auto_advance) noexcept
{
  const unsigned page_index =
    CommonInterface::GetUIState().pages.current_index;
  if (!WeatherMapOverlay::MutateOverlayPage(
        page_index, PageLayout::Overlay::SKYSIGHT,
        [auto_advance](PageLayout &page) noexcept {
          const auto *layer = GetLayer(page);
          int64_t time = PageLayout::SKYSIGHT_TIME_AUTO;
          if (!auto_advance && layer != nullptr) {
            if (layer->SupportsLiveTiles()) {
              const time_t latest = LatestLiveFloor(*layer);
              if (latest > 0)
                time = int64_t(latest);
            } else if (layer->forecast_time > 0) {
              time = int64_t(layer->forecast_time);
            }
          }
          if (page.skysight_time == time)
            return false;
          page.skysight_time = time;
          return true;
        }))
    return;

  AfterLiveChange(CommonInterface::GetUISettings().pages.pages[page_index]);
}

void
ApplyAutoAdvanceTime() noexcept
{
  if (const auto *page = GetCurrentPage(); page != nullptr)
    AfterLiveChange(*page);
}

void
EnableTimeAutoFromInput() noexcept
{
  SetTimeAutoAdvance(true);
  ApplyAutoAdvanceTime();
}

} // namespace SkySight
