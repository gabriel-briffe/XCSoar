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
  return CommonInterface::GetUIState().weather.rainbow_cursor.time <= 0;
#else
  return true;
#endif
}

void
RainbowControlsModel::SetPrimaryAutoAdvance(bool auto_advance) noexcept
{
#ifdef HAVE_HTTP
  auto &cursor = CommonInterface::SetUIState().weather.rainbow_cursor;
  if (auto_advance)
    cursor.time = PageLayout::RAINBOW_TIME_AUTO;
  CommonInterface::SetUIState().weather.rainbow.cursor_initialized =
    !auto_advance;
  Rainbow::PersistCursorToPage();
  Rainbow::ActivatePageOverlay();
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
  return GetPrimaryAutoAdvance()
    ? PrimaryLabelAction::NONE
    : PrimaryLabelAction::RESUME_AUTO;
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
  SetPrimaryAutoAdvance(true);
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
