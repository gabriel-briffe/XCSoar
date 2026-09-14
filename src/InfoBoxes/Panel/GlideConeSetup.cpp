// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeSetup.hpp"
#include "Widget/Widget.hpp"
#include "Form/Button.hpp"
#include "Form/Frame.hpp"
#include "Look/DialogLook.hpp"
#include "Screen/Layout.hpp"
#include "Interface.hpp"
#include "Computer/Settings.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "Language/Language.hpp"
#include "UIGlobals.hpp"
#include "util/StaticString.hxx"

#include <algorithm>
#include <array>
#include <memory>

/**
 * A single "Setup" panel for the glide cone: a "- <ratio> +" stepper row
 * on top and an Off/Single/Combined mode button row below (the active
 * mode is highlighted).
 */
class GlideConeSetupWidget final : public NullWidget {
  static constexpr std::array<GlideConeSettings::Mode, 3> MODES = {
    GlideConeSettings::Mode::OFF,
    GlideConeSettings::Mode::SINGLE,
    GlideConeSettings::Mode::COMBINED,
  };

  const DialogLook &look;

  std::unique_ptr<Button> minus, plus;
  std::unique_ptr<WndFrame> value;
  std::unique_ptr<std::array<Button, 3>> modes;

public:
  explicit GlideConeSetupWidget(const DialogLook &_look) noexcept
    :look(_look) {}

private:
  struct Cells {
    std::array<PixelRect, 3> top;
    std::array<PixelRect, 3> bottom;
  };

  static Cells Layout(const PixelRect &rc) noexcept {
    const int mid = (rc.top + rc.bottom) / 2;
    const auto thirds = [](int left, int right, int top, int bottom) {
      std::array<PixelRect, 3> cells{};
      const int width = right - left;
      for (unsigned i = 0; i < 3; ++i)
        cells[i] = PixelRect{left + int(i) * width / 3, top,
                             left + int(i + 1) * width / 3, bottom};
      return cells;
    };

    return {thirds(rc.left, rc.right, rc.top, mid),
            thirds(rc.left, rc.right, mid, rc.bottom)};
  }

  void UpdateValue() noexcept {
    const auto &gc = CommonInterface::GetComputerSettings().glide_cone;
    StaticString<16> text;
    text.Format("%d", int(gc.glide_ratio + 0.5));
    if (value != nullptr)
      value->SetText(text.c_str());
  }

  void UpdateModes() noexcept {
    if (modes == nullptr)
      return;
    const auto mode = CommonInterface::GetComputerSettings().glide_cone.mode;
    for (unsigned i = 0; i < MODES.size(); ++i)
      (*modes)[i].SetSelected(MODES[i] == mode);
  }

  void Adjust(int delta) noexcept {
    auto &gc = CommonInterface::SetComputerSettings().glide_cone;
    const double v = std::clamp(gc.glide_ratio + delta, 1.0, 200.0);
    gc.glide_ratio = v;
    Profile::Set(ProfileKeys::GlideConeGlideRatio, v);
    UpdateValue();
  }

  void SetMode(GlideConeSettings::Mode mode) noexcept {
    CommonInterface::SetComputerSettings().glide_cone.mode = mode;
    Profile::Set(ProfileKeys::GlideConeMode, int(mode));
    UpdateModes();
  }

public:
  PixelSize GetMinimumSize() const noexcept override {
    return {3u * Layout::GetMinimumControlHeight(),
            2u * Layout::GetMinimumControlHeight()};
  }

  PixelSize GetMaximumSize() const noexcept override {
    return {6u * Layout::GetMaximumControlHeight(),
            2u * Layout::GetMaximumControlHeight()};
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);

    WindowStyle style;
    style.Hide();

    WindowStyle button_style{style};
    button_style.TabStop();

    minus = std::make_unique<Button>(parent, look.button, "-", cells.top[0],
                                     button_style, [this](){ Adjust(-1); });
    value = std::make_unique<WndFrame>(parent, look, cells.top[1], style);
    value->SetAlignCenter();
    value->SetVAlignCenter();
    plus = std::make_unique<Button>(parent, look.button, "+", cells.top[2],
                                    button_style, [this](){ Adjust(1); });

    static constexpr const char *labels[] = {
      N_("Off"), N_("Single"), N_("Combined"),
    };
    modes = std::make_unique<std::array<Button, 3>>();
    for (unsigned i = 0; i < MODES.size(); ++i) {
      const auto mode = MODES[i];
      (*modes)[i].Create(parent, look.button, gettext(labels[i]),
                         cells.bottom[i], button_style,
                         [this, mode](){ SetMode(mode); });
    }

    UpdateValue();
    UpdateModes();
  }

  void Show(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    minus->MoveAndShow(cells.top[0]);
    value->MoveAndShow(cells.top[1]);
    plus->MoveAndShow(cells.top[2]);
    for (unsigned i = 0; i < modes->size(); ++i)
      (*modes)[i].MoveAndShow(cells.bottom[i]);
    UpdateValue();
    UpdateModes();
  }

  void Hide() noexcept override {
    minus->Hide();
    value->Hide();
    plus->Hide();
    for (auto &b : *modes)
      b.Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    minus->Move(cells.top[0]);
    value->Move(cells.top[1]);
    plus->Move(cells.top[2]);
    for (unsigned i = 0; i < modes->size(); ++i)
      (*modes)[i].Move(cells.bottom[i]);
  }

  bool SetFocus() noexcept override {
    plus->SetFocus();
    return true;
  }

  bool HasFocus() const noexcept override {
    if (minus->HasFocus() || plus->HasFocus())
      return true;
    for (const auto &b : *modes)
      if (b.HasFocus())
        return true;
    return false;
  }
};

constexpr std::array<GlideConeSettings::Mode, 3> GlideConeSetupWidget::MODES;

std::unique_ptr<Widget>
LoadGlideConeSetupPanel([[maybe_unused]] unsigned id)
{
  return std::make_unique<GlideConeSetupWidget>(UIGlobals::GetDialogLook());
}
