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
#include "UIGlobals.hpp"
#include "util/StaticString.hxx"

#include <algorithm>
#include <array>
#include <memory>

/**
 * A "- <value> +" stepper for the glide cone glide ratio.  Each button
 * adjusts the ratio by 1; recompute is debounced in the renderer.
 */
class GlideConeSetupWidget final : public NullWidget {
  const DialogLook &look;

  std::unique_ptr<Button> minus, plus;
  std::unique_ptr<WndFrame> value;

public:
  explicit GlideConeSetupWidget(const DialogLook &_look) noexcept
    :look(_look) {}

private:
  static std::array<PixelRect, 3> Layout(const PixelRect &rc) noexcept {
    const int width = rc.GetWidth();
    std::array<PixelRect, 3> cells{rc, rc, rc};
    cells[0].right = rc.left + width / 3;
    cells[1].left = cells[0].right;
    cells[1].right = rc.left + 2 * width / 3;
    cells[2].left = cells[1].right;
    return cells;
  }

  void UpdateValue() noexcept {
    const auto &gc = CommonInterface::GetComputerSettings().glide_cone;
    StaticString<16> text;
    text.Format("%d", int(gc.glide_ratio + 0.5));
    if (value != nullptr)
      value->SetText(text.c_str());
  }

  void Adjust(int delta) noexcept {
    auto &gc = CommonInterface::SetComputerSettings().glide_cone;
    const double v = std::clamp(gc.glide_ratio + delta, 1.0, 200.0);
    gc.glide_ratio = v;
    Profile::Set(ProfileKeys::GlideConeGlideRatio, v);
    UpdateValue();
  }

public:
  PixelSize GetMinimumSize() const noexcept override {
    return {3u * Layout::GetMinimumControlHeight(),
            Layout::GetMinimumControlHeight()};
  }

  PixelSize GetMaximumSize() const noexcept override {
    return {3u * Layout::GetMaximumControlHeight(),
            Layout::GetMaximumControlHeight()};
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);

    WindowStyle style;
    style.Hide();

    WindowStyle button_style{style};
    button_style.TabStop();

    minus = std::make_unique<Button>(parent, look.button, "-", cells[0],
                                     button_style, [this](){ Adjust(-1); });
    value = std::make_unique<WndFrame>(parent, look, cells[1], style);
    value->SetAlignCenter();
    value->SetVAlignCenter();
    plus = std::make_unique<Button>(parent, look.button, "+", cells[2],
                                    button_style, [this](){ Adjust(1); });

    UpdateValue();
  }

  void Show(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    minus->MoveAndShow(cells[0]);
    value->MoveAndShow(cells[1]);
    plus->MoveAndShow(cells[2]);
    UpdateValue();
  }

  void Hide() noexcept override {
    minus->Hide();
    value->Hide();
    plus->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    minus->Move(cells[0]);
    value->Move(cells[1]);
    plus->Move(cells[2]);
  }

  bool SetFocus() noexcept override {
    plus->SetFocus();
    return true;
  }

  bool HasFocus() const noexcept override {
    return minus->HasFocus() || plus->HasFocus();
  }
};

std::unique_ptr<Widget>
LoadGlideConeSetupPanel([[maybe_unused]] unsigned id)
{
  return std::make_unique<GlideConeSetupWidget>(UIGlobals::GetDialogLook());
}
