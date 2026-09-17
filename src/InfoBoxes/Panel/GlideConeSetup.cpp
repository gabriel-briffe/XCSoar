// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeSetup.hpp"
#include "Widget/Widget.hpp"
#include "Form/Button.hpp"
#include "Form/Frame.hpp"
#include "Look/DialogLook.hpp"
#include "Look/ButtonLook.hpp"
#include "Screen/Layout.hpp"
#include "Interface.hpp"
#include "Computer/Settings.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "Language/Language.hpp"
#include "UIGlobals.hpp"
#include "Renderer/TextButtonRenderer.hpp"
#include "util/StaticString.hxx"

#include <algorithm>
#include <array>
#include <memory>

namespace {

static constexpr const char *const GLIDE_CONE_MODE_LABELS[] = {
  N_("Off"), N_("Single"), N_("Combined"),
};

/**
 * Button look for exclusive toggles: active = XCSoar blue (selected),
 * inactive = greyed (disabled look) but still clickable.
 */
class ActiveTextButtonRenderer final : public TextButtonRenderer {
  bool active = false;

public:
  using TextButtonRenderer::TextButtonRenderer;

  void SetActive(bool _active) noexcept {
    active = _active;
  }

  void DrawButton(Canvas &canvas, const PixelRect &rc,
                  ButtonState state) const noexcept override {
    if (state != ButtonState::PRESSED)
      state = active ? ButtonState::SELECTED : ButtonState::DISABLED;
    TextButtonRenderer::DrawButton(canvas, rc, state);
  }
};

void
SetButtonActive(Button &button, bool active) noexcept
{
  auto &r = (ActiveTextButtonRenderer &)button.GetRenderer();
  r.SetActive(active);
  button.Invalidate();
}

std::unique_ptr<Button>
MakeActiveButton(ContainerWindow &parent, const ButtonLook &look,
                 const char *caption, const PixelRect &rc,
                 WindowStyle style, Button::Callback callback) noexcept
{
  auto button = std::make_unique<Button>();
  button->Create(parent, rc, style,
                 std::make_unique<ActiveTextButtonRenderer>(look, caption),
                 std::move(callback));
  return button;
}

} // namespace

/**
 * Setup tab: L/D stepper and Off/Single/Combined mode bar.
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
  std::array<std::unique_ptr<Button>, 3> modes;

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
    const auto mode = CommonInterface::GetComputerSettings().glide_cone.mode;
    for (unsigned i = 0; i < MODES.size(); ++i)
      if (modes[i] != nullptr)
        SetButtonActive(*modes[i], MODES[i] == mode);
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

    for (unsigned i = 0; i < MODES.size(); ++i) {
      const auto mode = MODES[i];
      modes[i] = MakeActiveButton(parent, look.button,
                                  gettext(GLIDE_CONE_MODE_LABELS[i]),
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
    for (unsigned i = 0; i < modes.size(); ++i)
      modes[i]->MoveAndShow(cells.bottom[i]);
    UpdateValue();
    UpdateModes();
  }

  void Hide() noexcept override {
    minus->Hide();
    value->Hide();
    plus->Hide();
    for (auto &b : modes)
      b->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    minus->Move(cells.top[0]);
    value->Move(cells.top[1]);
    plus->Move(cells.top[2]);
    for (unsigned i = 0; i < modes.size(); ++i)
      modes[i]->Move(cells.bottom[i]);
  }

  bool SetFocus() noexcept override {
    plus->SetFocus();
    return true;
  }

  bool HasFocus() const noexcept override {
    if (minus->HasFocus() || plus->HasFocus())
      return true;
    for (const auto &b : modes)
      if (b->HasFocus())
        return true;
    return false;
  }
};

constexpr std::array<GlideConeSettings::Mode, 3> GlideConeSetupWidget::MODES;

/**
 * Contours tab: On/Off bar for altitude contours.
 */
class GlideConeContoursWidget final : public NullWidget {
  const DialogLook &look;

  std::unique_ptr<Button> on_button, off_button;

public:
  explicit GlideConeContoursWidget(const DialogLook &_look) noexcept
    :look(_look) {}

private:
  struct Cells {
    PixelRect left, right;
  };

  static Cells Layout(const PixelRect &rc) noexcept {
    const int mid = (rc.left + rc.right) / 2;
    return {
      PixelRect{rc.left, rc.top, mid, rc.bottom},
      PixelRect{mid, rc.top, rc.right, rc.bottom},
    };
  }

  void UpdateButtons() noexcept {
    const bool on =
      CommonInterface::GetComputerSettings().glide_cone.contours;
    if (on_button != nullptr)
      SetButtonActive(*on_button, on);
    if (off_button != nullptr)
      SetButtonActive(*off_button, !on);
  }

  void SetContours(bool on) noexcept {
    auto &gc = CommonInterface::SetComputerSettings().glide_cone;
    gc.contours = on;
    Profile::Set(ProfileKeys::GlideConeContours, on);
    UpdateButtons();
  }

public:
  PixelSize GetMinimumSize() const noexcept override {
    return {2u * Layout::GetMinimumControlHeight(),
            Layout::GetMinimumControlHeight()};
  }

  PixelSize GetMaximumSize() const noexcept override {
    return {4u * Layout::GetMaximumControlHeight(),
            Layout::GetMaximumControlHeight()};
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);

    WindowStyle style;
    style.Hide();
    WindowStyle button_style{style};
    button_style.TabStop();

    on_button = MakeActiveButton(parent, look.button, _("On"),
                                 cells.left, button_style,
                                 [this](){ SetContours(true); });
    off_button = MakeActiveButton(parent, look.button, _("Off"),
                                  cells.right, button_style,
                                  [this](){ SetContours(false); });
    UpdateButtons();
  }

  void Show(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    on_button->MoveAndShow(cells.left);
    off_button->MoveAndShow(cells.right);
    UpdateButtons();
  }

  void Hide() noexcept override {
    on_button->Hide();
    off_button->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    on_button->Move(cells.left);
    off_button->Move(cells.right);
  }

  bool SetFocus() noexcept override {
    on_button->SetFocus();
    return true;
  }

  bool HasFocus() const noexcept override {
    return on_button->HasFocus() || off_button->HasFocus();
  }
};

std::unique_ptr<Widget>
LoadGlideConeSetupPanel([[maybe_unused]] unsigned id)
{
  return std::make_unique<GlideConeSetupWidget>(UIGlobals::GetDialogLook());
}

std::unique_ptr<Widget>
LoadGlideConeContoursPanel([[maybe_unused]] unsigned id)
{
  return std::make_unique<GlideConeContoursWidget>(UIGlobals::GetDialogLook());
}
