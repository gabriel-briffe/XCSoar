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
#include "GlideCone/GlideConeOptions.hpp"
#include "GlideCone/GlideConeStatus.hpp"
#include "ui/event/Timer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
 * Setup tab: L/D stepper, Off/Single/Combined, CPU/GPU, last compute.
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
  std::array<std::unique_ptr<Button>, 2> engines;
  std::unique_ptr<WndFrame> duration;
  /** Refresh "Last compute" while this tab is visible. */
  UI::Timer refresh_timer{[this]{
    UpdateDuration();
    refresh_timer.Schedule(std::chrono::milliseconds{250});
  }};

public:
  explicit GlideConeSetupWidget(const DialogLook &_look) noexcept
    :look(_look) {}

private:
  struct Cells {
    std::array<PixelRect, 3> ratio;
    std::array<PixelRect, 3> mode;
    PixelRect cpu, gpu;
    PixelRect info;
  };

  static Cells Layout(const PixelRect &rc) noexcept {
    const int y1 = rc.top + rc.GetHeight() / 4;
    const int y2 = rc.top + 2 * rc.GetHeight() / 4;
    const int y3 = rc.top + 3 * rc.GetHeight() / 4;
    const int half = (rc.left + rc.right) / 2;
    const auto thirds = [](int left, int right, int top, int bottom) {
      std::array<PixelRect, 3> cells{};
      const int width = right - left;
      for (unsigned i = 0; i < 3; ++i)
        cells[i] = PixelRect{left + int(i) * width / 3, top,
                             left + int(i + 1) * width / 3, bottom};
      return cells;
    };

    return {thirds(rc.left, rc.right, rc.top, y1),
            thirds(rc.left, rc.right, y1, y2),
            PixelRect{rc.left, y2, half, y3},
            PixelRect{half, y2, rc.right, y3},
            PixelRect{rc.left, y3, rc.right, rc.bottom}};
  }

  void UpdateValue() noexcept {
    const auto &gc = CommonInterface::GetComputerSettings().glide_cone;
    StaticString<16> text;
    text.Format("%d", int(gc.glide_ratio + 0.5));
    if (value != nullptr)
      value->SetText(text.c_str());
  }

  void UpdateModes() noexcept {
    const auto &gc = CommonInterface::GetComputerSettings().glide_cone;
    for (unsigned i = 0; i < MODES.size(); ++i)
      if (modes[i] != nullptr)
        SetButtonActive(*modes[i], MODES[i] == gc.mode);

    const bool cpu = gc.cone_engine == GlideConeSettings::ConeEngine::CPU;
    if (engines[0] != nullptr)
      SetButtonActive(*engines[0], cpu);
    if (engines[1] != nullptr)
      SetButtonActive(*engines[1], !cpu);
  }

  void UpdateDuration() noexcept {
    if (duration == nullptr)
      return;
    StaticString<64> text;
    if (const auto ms = GlideConeStatus::LastComputeMs())
      text.UnsafeFormat(_("Last compute: %u ms"), *ms);
    else
      text.UnsafeFormat("%s", _("Last compute: —"));
    duration->SetText(text.c_str());
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

  void SetEngine(GlideConeSettings::ConeEngine engine) noexcept {
    auto &gc = CommonInterface::SetComputerSettings().glide_cone;
    if (gc.cone_engine == engine) {
      UpdateModes();
      return;
    }
    gc.cone_engine = engine;
    Profile::Set(ProfileKeys::GlideConeEngine, int(engine));
    UpdateModes();
  }

public:
  PixelSize GetMinimumSize() const noexcept override {
    return {3u * Layout::GetMinimumControlHeight(),
            4u * Layout::GetMinimumControlHeight()};
  }

  PixelSize GetMaximumSize() const noexcept override {
    return {6u * Layout::GetMaximumControlHeight(),
            4u * Layout::GetMaximumControlHeight()};
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);

    WindowStyle style;
    style.Hide();

    WindowStyle button_style{style};
    button_style.TabStop();

    minus = std::make_unique<Button>(parent, look.button, "-", cells.ratio[0],
                                     button_style, [this](){ Adjust(-1); });
    value = std::make_unique<WndFrame>(parent, look, cells.ratio[1], style);
    value->SetAlignCenter();
    value->SetVAlignCenter();
    plus = std::make_unique<Button>(parent, look.button, "+", cells.ratio[2],
                                    button_style, [this](){ Adjust(1); });

    for (unsigned i = 0; i < MODES.size(); ++i) {
      const auto mode = MODES[i];
      modes[i] = MakeActiveButton(parent, look.button,
                                  gettext(GLIDE_CONE_MODE_LABELS[i]),
                                  cells.mode[i], button_style,
                                  [this, mode](){ SetMode(mode); });
    }

    engines[0] = MakeActiveButton(parent, look.button, _("CPU"),
                                  cells.cpu, button_style,
                                  [this](){
                                    SetEngine(GlideConeSettings::ConeEngine::CPU);
                                  });
    engines[1] = MakeActiveButton(parent, look.button, _("GPU"),
                                  cells.gpu, button_style,
                                  [this](){
                                    SetEngine(GlideConeSettings::ConeEngine::GPU);
                                  });
    duration = std::make_unique<WndFrame>(parent, look, cells.info, style);
    duration->SetVAlignCenter();

    UpdateValue();
    UpdateModes();
    UpdateDuration();
  }

  void Show(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    minus->MoveAndShow(cells.ratio[0]);
    value->MoveAndShow(cells.ratio[1]);
    plus->MoveAndShow(cells.ratio[2]);
    for (unsigned i = 0; i < modes.size(); ++i)
      modes[i]->MoveAndShow(cells.mode[i]);
    engines[0]->MoveAndShow(cells.cpu);
    engines[1]->MoveAndShow(cells.gpu);
    duration->MoveAndShow(cells.info);
    UpdateValue();
    UpdateModes();
    UpdateDuration();
    refresh_timer.Schedule(std::chrono::milliseconds{250});
  }

  void Hide() noexcept override {
    refresh_timer.Cancel();
    minus->Hide();
    value->Hide();
    plus->Hide();
    for (auto &b : modes)
      b->Hide();
    for (auto &b : engines)
      b->Hide();
    duration->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    minus->Move(cells.ratio[0]);
    value->Move(cells.ratio[1]);
    plus->Move(cells.ratio[2]);
    for (unsigned i = 0; i < modes.size(); ++i)
      modes[i]->Move(cells.mode[i]);
    engines[0]->Move(cells.cpu);
    engines[1]->Move(cells.gpu);
    duration->Move(cells.info);
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
    return engines[0]->HasFocus() || engines[1]->HasFocus();
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


class GlideConeOptionsWidget final : public NullWidget {
  const DialogLook &look;
  std::array<std::unique_ptr<Button>, 3> modes;
  std::array<std::unique_ptr<Button>, 2> engines;
  std::unique_ptr<WndFrame> duration;
  /** Refresh "Last compute" while this tab is visible. */
  UI::Timer refresh_timer{[this]{
    UpdateDuration();
    refresh_timer.Schedule(std::chrono::milliseconds{250});
  }};

  struct Cells {
    PixelRect off, once, routine;
    PixelRect cpu, gpu;
    PixelRect info;
  };

  static Cells Layout(const PixelRect &rc) noexcept {
    const int mid1 = (rc.left * 2 + rc.right) / 3;
    const int mid2 = (rc.left + rc.right * 2) / 3;
    const int half = (rc.left + rc.right) / 2;
    const int y1 = rc.top + rc.GetHeight() / 3;
    const int y2 = rc.top + 2 * rc.GetHeight() / 3;
    return {
      PixelRect{rc.left, rc.top, mid1, y1},
      PixelRect{mid1, rc.top, mid2, y1},
      PixelRect{mid2, rc.top, rc.right, y1},
      PixelRect{rc.left, y1, half, y2},
      PixelRect{half, y1, rc.right, y2},
      PixelRect{rc.left, y2, rc.right, rc.bottom},
    };
  }

  void UpdateButtons() noexcept {
    const auto &gc = CommonInterface::GetComputerSettings().glide_cone;
    const bool mode_on[] = {
      gc.options_mode == GlideConeSettings::OptionsMode::OFF,
      gc.options_mode == GlideConeSettings::OptionsMode::ONCE,
      gc.options_mode == GlideConeSettings::OptionsMode::ROUTINE,
    };
    for (std::size_t i = 0; i < modes.size(); ++i)
      if (modes[i] != nullptr)
        SetButtonActive(*modes[i], mode_on[i]);

    const bool cpu = gc.options_engine == GlideConeSettings::OptionsEngine::CPU;
    if (engines[0] != nullptr)
      SetButtonActive(*engines[0], cpu);
    if (engines[1] != nullptr)
      SetButtonActive(*engines[1], !cpu);
  }

  void UpdateDuration() noexcept {
    if (duration == nullptr)
      return;
    StaticString<64> text;
    if (const auto ms = GlideConeOptions::LastComputeMs())
      text.UnsafeFormat(_("Last compute: %u ms"), *ms);
    else
      text.UnsafeFormat("%s", _("Last compute: —"));
    duration->SetText(text.c_str());
  }

  void SetMode(GlideConeSettings::OptionsMode mode) noexcept {
    auto &gc = CommonInterface::SetComputerSettings().glide_cone;
    gc.options_mode = mode;
    Profile::Set(ProfileKeys::GlideConeOptionsMode, int(mode));
    if (mode == GlideConeSettings::OptionsMode::OFF)
      GlideConeOptions::Clear();
    else
      GlideConeOptions::RequestOnce();
    UpdateButtons();
    UpdateDuration();
  }

  void SetEngine(GlideConeSettings::OptionsEngine engine) noexcept {
    auto &gc = CommonInterface::SetComputerSettings().glide_cone;
    if (gc.options_engine == engine) {
      UpdateButtons();
      return;
    }
    gc.options_engine = engine;
    Profile::Set(ProfileKeys::GlideConeOptionsEngine, int(engine));
    GlideConeOptions::AbandonGpu();
    GlideConeOptions::AbandonCpu();
    if (gc.options_mode != GlideConeSettings::OptionsMode::OFF)
      GlideConeOptions::RequestOnce();
    UpdateButtons();
    UpdateDuration();
  }

public:
  explicit GlideConeOptionsWidget(const DialogLook &_look) noexcept
    :look(_look) {}

  PixelSize GetMinimumSize() const noexcept override {
    return {3u * Layout::GetMinimumControlHeight(),
            3u * Layout::GetMinimumControlHeight()};
  }

  PixelSize GetMaximumSize() const noexcept override {
    return {6u * Layout::GetMaximumControlHeight(),
            3u * Layout::GetMaximumControlHeight()};
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    WindowStyle style;
    style.Hide();
    WindowStyle button_style{style};
    button_style.TabStop();
    modes[0] = MakeActiveButton(parent, look.button, _("Off"),
                                cells.off, button_style,
                                [this](){ SetMode(GlideConeSettings::OptionsMode::OFF); });
    modes[1] = MakeActiveButton(parent, look.button, _("One time"),
                                cells.once, button_style,
                                [this](){ SetMode(GlideConeSettings::OptionsMode::ONCE); });
    modes[2] = MakeActiveButton(parent, look.button, _("Routine"),
                                cells.routine, button_style,
                                [this](){ SetMode(GlideConeSettings::OptionsMode::ROUTINE); });
    engines[0] = MakeActiveButton(parent, look.button, _("CPU"),
                                  cells.cpu, button_style,
                                  [this](){
                                    SetEngine(GlideConeSettings::OptionsEngine::CPU);
                                  });
    engines[1] = MakeActiveButton(parent, look.button, _("GPU"),
                                  cells.gpu, button_style,
                                  [this](){
                                    SetEngine(GlideConeSettings::OptionsEngine::GPU);
                                  });
    duration = std::make_unique<WndFrame>(parent, look, cells.info, style);
    duration->SetVAlignCenter();
    UpdateButtons();
    UpdateDuration();
  }

  void Show(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    modes[0]->MoveAndShow(cells.off);
    modes[1]->MoveAndShow(cells.once);
    modes[2]->MoveAndShow(cells.routine);
    engines[0]->MoveAndShow(cells.cpu);
    engines[1]->MoveAndShow(cells.gpu);
    duration->MoveAndShow(cells.info);
    UpdateButtons();
    UpdateDuration();
    refresh_timer.Schedule(std::chrono::milliseconds{250});
  }

  void Hide() noexcept override {
    refresh_timer.Cancel();
    for (auto &button : modes)
      button->Hide();
    for (auto &button : engines)
      button->Hide();
    duration->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    const auto cells = Layout(rc);
    modes[0]->Move(cells.off);
    modes[1]->Move(cells.once);
    modes[2]->Move(cells.routine);
    engines[0]->Move(cells.cpu);
    engines[1]->Move(cells.gpu);
    duration->Move(cells.info);
  }

  bool SetFocus() noexcept override {
    modes[0]->SetFocus();
    return true;
  }

  bool HasFocus() const noexcept override {
    return modes[0]->HasFocus() || modes[1]->HasFocus() ||
      modes[2]->HasFocus() || engines[0]->HasFocus() ||
      engines[1]->HasFocus();
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

std::unique_ptr<Widget>
LoadGlideConeOptionsPanel([[maybe_unused]] unsigned id)
{
  return std::make_unique<GlideConeOptionsWidget>(UIGlobals::GetDialogLook());
}
