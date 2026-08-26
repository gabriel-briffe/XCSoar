// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RainbowControlsModel.hpp"
#include "CursorBarLabels.hpp"
#include "Weather/Features.hpp"
#include "Interface.hpp"
#include "UIState.hpp"
#include "PageSettings.hpp"
#include "PageActions.hpp"
#include "Language/Language.hpp"

#include <chrono>

#ifdef HAVE_HTTP
#include "Weather/Rainbow/FieldControls.hpp"
#endif

namespace WeatherMapOverlay {

void
RainbowControlsModel::OnShow() noexcept
{
#ifdef HAVE_HTTP
  Rainbow::ApplyCursorFromPageLayout(PageActions::GetCurrentLayout());
  Rainbow::ActivatePageOverlay();
  Notify(ControlsUpdate::LABELS);
#endif
}

void
RainbowControlsModel::OnHide() noexcept
{
#ifdef HAVE_HTTP
  const bool replaying = replay_step > 0;
  CancelTimeReplay();
  if (replaying)
    SetPrimaryAutoAdvance(true);
#endif
}

void
RainbowControlsModel::FormatPrimaryLabel(StaticString<64> &text) const noexcept
{
#ifdef HAVE_HTTP
  Rainbow::FormatTimeLabel(text);
  if (text.empty())
    text = "Rainbow";
#else
  text = "Rainbow";
#endif
}

void
RainbowControlsModel::FormatSecondaryLabel(StaticString<64> &text) const noexcept
{
#ifdef HAVE_HTTP
  Rainbow::FormatLayerLabel(text);
#else
  text = NoForecastHint();
#endif
}

bool
RainbowControlsModel::HasPrimaryData() const noexcept
{
#ifdef HAVE_HTTP
  return Rainbow::HasActiveOverlay();
#else
  return false;
#endif
}

bool
RainbowControlsModel::HasSecondaryData() const noexcept
{
  return HasPrimaryData();
}

bool
RainbowControlsModel::StepPrimary(int delta) noexcept
{
#ifdef HAVE_HTTP
  CancelTimeReplay();
  return Rainbow::StepTime(delta);
#else
  (void)delta;
  return false;
#endif
}

bool
RainbowControlsModel::StepSecondary(int delta) noexcept
{
#ifdef HAVE_HTTP
  return Rainbow::StepLayer(delta);
#else
  (void)delta;
  return false;
#endif
}

bool
RainbowControlsModel::GetPrimaryAutoAdvance() const noexcept
{
#ifdef HAVE_HTTP
  return Rainbow::GetTimeAutoAdvance();
#else
  return true;
#endif
}

void
RainbowControlsModel::SetPrimaryAutoAdvance(bool auto_advance) noexcept
{
#ifdef HAVE_HTTP
  if (auto_advance)
    Rainbow::SetTimeAutoAdvance(true, 0);
  else
    Rainbow::SetTimeAutoAdvance(false, 0);
  Notify(ControlsUpdate::OVERLAY);
#else
  (void)auto_advance;
#endif
}

void
RainbowControlsModel::ApplyPrimaryAutoAdvance() noexcept
{
  SetPrimaryAutoAdvance(true);
}

void
RainbowControlsModel::EnablePrimaryAutoFromInput() noexcept
{
  SetPrimaryAutoAdvance(true);
}

PrimaryLabelAction
RainbowControlsModel::GetPrimaryLabelAction() const noexcept
{
#ifdef HAVE_HTTP
  if (!Rainbow::HasActiveOverlay())
    return PrimaryLabelAction::NONE;

  if (!GetPrimaryAutoAdvance())
    return PrimaryLabelAction::RESUME_AUTO;

  if (Rainbow::CanReplayLiveTime())
    return PrimaryLabelAction::REPLAY;

  return PrimaryLabelAction::NONE;
#else
  return PrimaryLabelAction::NONE;
#endif
}

SecondaryLabelAction
RainbowControlsModel::GetSecondaryLabelAction() const noexcept
{
  return SecondaryLabelAction::NONE;
}

void
RainbowControlsModel::OpenPrimaryPicker() noexcept
{
}

SecondaryPickerResult
RainbowControlsModel::OpenSecondaryPicker() noexcept
{
  return SecondaryPickerResult::NONE;
}

void
RainbowControlsModel::ResumePrimaryAuto() noexcept
{
#ifdef HAVE_HTTP
  CancelTimeReplay();

  if (GetPrimaryAutoAdvance())
    return;

  SetPrimaryAutoAdvance(true);
  Notify(ControlsUpdate::OVERLAY);
#endif
}

void
RainbowControlsModel::CancelTimeReplay() noexcept
{
  replay_timer.Cancel();
  replay_step = 0;
  replay_reference = 0;
}

void
RainbowControlsModel::AdvanceTimeReplay() noexcept
{
#ifdef HAVE_HTTP
  if (replay_reference <= 0)
    return;

  if (replay_step == 1) {
    if (Rainbow::SetPageTime(replay_reference -
                             Rainbow::SNAPSHOT_INTERVAL_SECONDS))
      Notify(ControlsUpdate::OVERLAY);
    replay_step = 2;
    replay_timer.Schedule(std::chrono::milliseconds{500});
    return;
  }

  if (replay_step == 2) {
    const int64_t live = replay_reference;
    CancelTimeReplay();
    Rainbow::SetTimeAutoAdvance(true, live);
    Notify(ControlsUpdate::OVERLAY);
  }
#else
  CancelTimeReplay();
#endif
}

void
RainbowControlsModel::ReplayPrimary() noexcept
{
#ifdef HAVE_HTTP
  if (replay_timer.IsPending())
    return;

  if (!Rainbow::CanReplayLiveTime())
    return;

  replay_reference = Rainbow::GetLiveReferenceTime();
  if (replay_reference <= 0)
    return;

  if (Rainbow::SetPageTime(replay_reference -
                           2 * Rainbow::SNAPSHOT_INTERVAL_SECONDS))
    Notify(ControlsUpdate::OVERLAY);

  replay_step = 1;
  replay_timer.Schedule(std::chrono::milliseconds{500});
#endif
}

void
RainbowControlsModel::RefreshOverlay() noexcept
{
#ifdef HAVE_HTTP
  Rainbow::ActivatePageOverlay();
  Notify(ControlsUpdate::LABELS);
#endif
}

} // namespace WeatherMapOverlay
