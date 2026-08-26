// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SkySightControlsModel.hpp"

#include "DataGlobals.hpp"
#include "Interface.hpp"
#include "PageActions.hpp"
#include "Language/Language.hpp"
#include "UIState.hpp"
#include "Weather/SkySight/FieldControls.hpp"
#include "Weather/SkySight/LiveTileUtils.hpp"
#include "Weather/SkySight/SkySightClient.hpp"
#include <chrono>

namespace WeatherMapOverlay {

SkySightControlsModel::SkySightControlsModel(
  std::shared_ptr<SkySightClient> _skysight) noexcept
  :skysight(std::move(_skysight)) {}

void
SkySightControlsModel::OnShow() noexcept
{
  SkySight::ApplyCursorFromPageLayout(PageActions::GetCurrentLayout());
  countdown_timer.Schedule(std::chrono::seconds{1});
}

void
SkySightControlsModel::OnHide() noexcept
{
  countdown_timer.Cancel();
  const bool replaying = replay_step > 0;
  CancelTimeReplay();
  if (replaying)
    SetPrimaryAutoAdvance(true);
  dynamic_status_visible = false;
}

const SkySight::Layer *
SkySightControlsModel::GetLayer() const noexcept
{
  const auto &page = PageActions::GetCurrentLayout();
  return skysight != nullptr && page.UsesSkySightOverlay()
    ? skysight->GetSelectedLayer(page.skysight_overlay.c_str())
    : nullptr;
}

void
SkySightControlsModel::FormatPrimaryLabel(StaticString<64> &text) const noexcept
{
  if (skysight != nullptr && skysight->IsThrottled()) {
    text.Format(_("Download limit: retry in %u s"),
                unsigned(skysight->GetThrottleRemainingSeconds()));
    return;
  }

  if (skysight != nullptr) {
    const auto retry = skysight->GetDatafilesRetryRemainingSeconds();
    if (retry > 0) {
      text.Format(_("Retry download in %u s"), unsigned(retry));
      return;
    }
  }

  SkySight::FormatTimeLabelForPage(text, PageActions::GetCurrentLayout());
  if (const auto *layer = GetLayer();
      layer != nullptr && layer->SupportsLiveTiles() &&
      skysight->IsLiveViewUpdating(layer->id)) {
    const auto displayed =
      CommonInterface::GetUIState().weather.skysight_cursor.displayed_time;
    if (displayed <= 0)
      text = C_("Status", "Live (updating...)");
  }
}

void
SkySightControlsModel::FormatSecondaryLabel(StaticString<64> &text) const noexcept
{
  SkySight::FormatLayerLabelForPage(text, PageActions::GetCurrentLayout());
}

bool
SkySightControlsModel::HasPrimaryData() const noexcept
{
  return SkySight::HasSelectedTimeData();
}

bool
SkySightControlsModel::IsPrimaryEnabled() const noexcept
{
  return SkySight::IsTimeSelectable();
}

bool
SkySightControlsModel::HasSecondaryData() const noexcept
{
  return SkySight::HasSelectedLayer();
}

bool
SkySightControlsModel::StepPrimary(int delta) noexcept
{
  CancelTimeReplay();
  return SkySight::StepTime(delta);
}

bool
SkySightControlsModel::StepSecondary(int delta) noexcept
{
  return SkySight::StepLayer(delta);
}

void
SkySightControlsModel::UpdateCountdownLabel() noexcept
{
  const auto *layer = GetLayer();
  const bool waiting = skysight != nullptr &&
    (skysight->IsThrottled() ||
     skysight->GetDatafilesRetryRemainingSeconds() > 0 ||
     (layer != nullptr && skysight->IsLiveViewUpdating(layer->id)));

  if (waiting || dynamic_status_visible)
    Notify(ControlsUpdate::LABELS);

  dynamic_status_visible = waiting;
}

bool
SkySightControlsModel::GetPrimaryAutoAdvance() const noexcept
{
  return SkySight::GetTimeAutoAdvance();
}

void
SkySightControlsModel::SetPrimaryAutoAdvance(bool auto_advance) noexcept
{
  SkySight::SetTimeAutoAdvance(auto_advance);
}

void
SkySightControlsModel::ApplyPrimaryAutoAdvance() noexcept
{
  SkySight::ApplyAutoAdvanceTime();
}

void
SkySightControlsModel::EnablePrimaryAutoFromInput() noexcept
{
  SkySight::EnableTimeAutoFromInput();
  NotifyOverlay();
}

PrimaryLabelAction
SkySightControlsModel::GetPrimaryLabelAction() const noexcept
{
  if (!SkySight::HasSelectedLayer())
    return PrimaryLabelAction::OPEN_SETUP;
  if (!SkySight::IsTimeSelectable())
    return PrimaryLabelAction::NONE;

  if (const auto *layer = GetLayer();
      layer != nullptr && layer->SupportsLiveTiles()) {
    if (!SkySight::GetTimeAutoAdvance())
      return PrimaryLabelAction::RESUME_AUTO;
    if (SkySight::CanReplayLiveTime())
      return PrimaryLabelAction::REPLAY;
  }

  return PrimaryLabelAction::OPEN_PICKER;
}

SecondaryLabelAction
SkySightControlsModel::GetSecondaryLabelAction() const noexcept
{
  return SecondaryLabelAction::OPEN_PICKER;
}

void
SkySightControlsModel::OpenPrimaryPicker() noexcept
{
  SkySight::OpenTimePicker();
  NotifyOverlay();
}

SecondaryPickerResult
SkySightControlsModel::OpenSecondaryPicker() noexcept
{
  return HandleSecondaryFieldPicker(SkySight::OpenLayerPicker(true), [this] {
    Notify(ControlsUpdate::OVERLAY);
  });
}

void
SkySightControlsModel::ResumePrimaryAuto() noexcept
{
  CancelTimeReplay();

  if (GetPrimaryAutoAdvance())
    return;

  SetPrimaryAutoAdvance(true);
  Notify(ControlsUpdate::OVERLAY);
}

void
SkySightControlsModel::CancelTimeReplay() noexcept
{
  replay_timer.Cancel();
  replay_step = 0;
  replay_reference = 0;
}

void
SkySightControlsModel::AdvanceTimeReplay() noexcept
{
  if (replay_reference <= 0)
    return;

  if (replay_step == 1) {
    if (SkySight::SetPageTime(int64_t(replay_reference -
                                    SkySight::LIVE_TILE_INTERVAL_SECONDS)))
      Notify(ControlsUpdate::OVERLAY);
    replay_step = 2;
    replay_timer.Schedule(std::chrono::milliseconds{500});
    return;
  }

  if (replay_step == 2) {
    CancelTimeReplay();
    SetPrimaryAutoAdvance(true);
    Notify(ControlsUpdate::OVERLAY);
  }
}

void
SkySightControlsModel::ReplayPrimary() noexcept
{
  if (replay_timer.IsPending())
    return;

  if (!SkySight::CanReplayLiveTime()) {
    OpenPrimaryPicker();
    return;
  }

  replay_reference = SkySight::GetLiveReferenceTime(
    PageActions::GetCurrentLayout());
  if (replay_reference <= 0)
    return;

  if (SkySight::SetPageTime(int64_t(replay_reference -
                                    2 * SkySight::LIVE_TILE_INTERVAL_SECONDS)))
    Notify(ControlsUpdate::OVERLAY);

  replay_step = 1;
  replay_timer.Schedule(std::chrono::milliseconds{500});
}

void
SkySightControlsModel::RefreshOverlay() noexcept
{
}

} // namespace WeatherMapOverlay
