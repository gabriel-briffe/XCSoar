// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "ControlsModel.hpp"

#include "util/StaticString.hxx"
#include "ui/event/Timer.hpp"

namespace WeatherMapOverlay {

class RainbowControlsModel final : public ControlsModel {
  UI::Timer replay_timer{[this]{ AdvanceTimeReplay(); }};
  int64_t replay_reference = 0;
  unsigned replay_step = 0;

public:
  void OnShow() noexcept override;
  void OnHide() noexcept override;

  void FormatPrimaryLabel(StaticString<64> &text) const noexcept override;
  void FormatSecondaryLabel(StaticString<64> &text) const noexcept override;

  [[nodiscard]]
  bool HasPrimaryData() const noexcept override;
  [[nodiscard]]
  bool HasSecondaryData() const noexcept override;

  [[nodiscard]]
  bool StepPrimary(int delta) noexcept override;
  [[nodiscard]]
  bool StepSecondary(int delta) noexcept override;

  [[nodiscard]]
  bool GetPrimaryAutoAdvance() const noexcept override;
  void SetPrimaryAutoAdvance(bool auto_advance) noexcept override;
  void ApplyPrimaryAutoAdvance() noexcept override;
  void EnablePrimaryAutoFromInput() noexcept override;

  [[nodiscard]]
  PrimaryLabelAction GetPrimaryLabelAction() const noexcept override;
  [[nodiscard]]
  SecondaryLabelAction GetSecondaryLabelAction() const noexcept override;
  void OpenPrimaryPicker() noexcept override;
  [[nodiscard]]
  SecondaryPickerResult OpenSecondaryPicker() noexcept override;
  void ResumePrimaryAuto() noexcept override;
  void ReplayPrimary() noexcept override;

  void RefreshOverlay() noexcept override;

private:
  void CancelTimeReplay() noexcept;
  void AdvanceTimeReplay() noexcept;
};

} // namespace WeatherMapOverlay
