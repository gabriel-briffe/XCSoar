// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "ShowButton.hpp"
#include "Renderer/ButtonRenderer.hpp"
#include "Renderer/SymbolButtonRenderer.hpp"
#include "Look/ButtonLook.hpp"
#include "Input/InputEvents.hpp"
#include "Interface.hpp"
#include "UISettings.hpp"
#include "Pan.hpp"
#include "PageActions.hpp"
#include "Screen/Layout.hpp"
#include "ui/canvas/Brush.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Color.hpp"
#include "ui/dim/BulkPoint.hpp"
#include "util/Macros.hpp"

#include <algorithm>
#include <memory>

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Scope.hpp"
#endif

#ifdef ANDROID
#include "Hardware/RotateDisplay.hpp"
#include "DisplayOrientation.hpp"
#include "Android/Main.hpp"
#include "Android/NativeView.hpp"
#include "Renderer/BitmapButtonRenderer.hpp"
#include "java/Global.hxx"
#endif

/**
 * Map overlay buttons (menu, QuickMenu, zoom) are hidden on special
 * pages; see PageActions::AllowMapOverlayButtons().
 */
class ShowMapOverlayButtonRenderer : public ButtonRenderer {
  std::unique_ptr<ButtonRenderer> inner;

public:
  explicit ShowMapOverlayButtonRenderer(std::unique_ptr<ButtonRenderer> _inner) noexcept
    :inner(std::move(_inner)) {}

  unsigned GetMinimumButtonWidth() const noexcept override {
    return inner->GetMinimumButtonWidth();
  }

  void DrawButton(Canvas &canvas, const PixelRect &rc,
                  ButtonState state) const noexcept override {
    if (!PageActions::AllowMapOverlayButtons())
      return;

    inner->DrawButton(canvas, rc, state);
  }
};

static std::unique_ptr<ButtonRenderer>
MakeMapOverlaySymbolButton(const ButtonLook &look,
                           const char *caption) noexcept
{
  return std::make_unique<ShowMapOverlayButtonRenderer>(
    std::make_unique<SymbolButtonRenderer>(look, caption));
}

/**
 * Pink marker for an invisible map touch target.  Opacity comes from
 * #UISettings::touch_areas_transparency (0 = solid, 100 = hidden).
 */
static void
DrawTouchAreaMarker(Canvas &canvas, const PixelRect &rc) noexcept
{
  const unsigned transparency =
    CommonInterface::GetUISettings().touch_areas_transparency;
  if (transparency >= 100)
    return;

  const uint8_t alpha = uint8_t((100u - transparency) * 255u / 100u);
  const Color fill = Color(0xff, 0x69, 0xb4).WithAlpha(alpha);

#ifdef ENABLE_OPENGL
  const ScopeAlphaBlend alpha_blend;
#endif
  canvas.DrawFilledRectangle(rc, fill);
}

/**
 * Invisible map touch target; optionally draws a pink area marker.
 */
class MapOverlayInvisibleButtonRenderer final : public ButtonRenderer {
public:
  void DrawButton(Canvas &canvas, const PixelRect &rc,
                  ButtonState) const noexcept override
  {
    DrawTouchAreaMarker(canvas, rc);
  }
};

/**
 * Bottom-center cross section / weather toggle: pink touch marker
 * (inset), plus a tiny black up-triangle flush with the map bottom to
 * locate the hit target.
 */
class BottomAreaToggleButtonRenderer final : public ButtonRenderer {
public:
  void DrawButton(Canvas &canvas, const PixelRect &rc,
                  ButtonState) const noexcept override
  {
    const unsigned padding = Layout::GetTextPadding();
    PixelRect marker = rc;
    if (padding > 0 && marker.GetHeight() > padding)
      marker.bottom -= int(padding);
    DrawTouchAreaMarker(canvas, marker);

    /* Base on the last map pixel row; tip points up. */
    const int mid_x = (rc.left + rc.right) / 2;
    const int base_y = rc.bottom - 1;
    const int half_w = std::max(2, int(Layout::Scale(4)));
    const int height = std::max(2, int(Layout::Scale(3)));
    const BulkPixelPoint triangle[] = {
      { mid_x, base_y - height },
      { mid_x - half_w, base_y },
      { mid_x + half_w, base_y },
    };

    const Brush brush(COLOR_BLACK);
    canvas.SelectNullPen();
    canvas.Select(brush);
    canvas.DrawTriangleFan(triangle, ARRAY_SIZE(triangle));
  }
};

/**
 * Map overlay QuickMenu button; draws the bolt, or a pink touch-area
 * marker when configured as transparent.
 */
class QuickMenuOverlayButtonRenderer final : public ButtonRenderer {
  SymbolButtonRenderer inner;

public:
  explicit QuickMenuOverlayButtonRenderer(const ButtonLook &look) noexcept
    :inner(look, "q") {}

  unsigned GetMinimumButtonWidth() const noexcept override {
    return inner.GetMinimumButtonWidth();
  }

  void DrawButton(Canvas &canvas, const PixelRect &rc,
                  ButtonState state) const noexcept override
  {
    if (CommonInterface::GetUISettings().transparent_quickmenu_button) {
      DrawTouchAreaMarker(canvas, rc);
      return;
    }

    inner.DrawButton(canvas, rc, state);
  }
};

static std::unique_ptr<ButtonRenderer>
MakeQuickMenuOverlayButton(const ButtonLook &look) noexcept
{
  return std::make_unique<ShowMapOverlayButtonRenderer>(
    std::make_unique<QuickMenuOverlayButtonRenderer>(look));
}

void
ShowMenuButton::Create(ContainerWindow &parent, const ButtonLook &look,
                       const PixelRect &rc, WindowStyle style) noexcept
{
  Button::Create(parent, rc, style, MakeMapOverlaySymbolButton(look, "h"));
}

bool
ShowMenuButton::OnClicked() noexcept
{
  InputEvents::ShowMenu();
  return true;
}

void
ShowQuickMenuButton::Create(ContainerWindow &parent, const ButtonLook &look,
                            const PixelRect &rc,
                            WindowStyle style) noexcept
{
  Button::Create(parent, rc, style, MakeQuickMenuOverlayButton(look));
}

bool
ShowQuickMenuButton::OnClicked() noexcept
{
  InputEvents::eventQuickMenu(nullptr);
  return true;
}

void
ShowPanNorthUpButton::Create(ContainerWindow &parent,
                             [[maybe_unused]] const ButtonLook &look,
                             const PixelRect &rc,
                             WindowStyle style) noexcept
{
  Button::Create(parent, rc, style,
                 std::make_unique<MapOverlayInvisibleButtonRenderer>());
}

bool
ShowPanNorthUpButton::OnClicked() noexcept
{
  if (IsPanning())
    SetPanNorthUp();
  else
    InputEvents::eventOrientationToggle("toggle");
  return true;
}

void
ShowAirspaceToggleButton::Create(ContainerWindow &parent,
                                 [[maybe_unused]] const ButtonLook &look,
                                 const PixelRect &rc,
                                 WindowStyle style) noexcept
{
  Button::Create(parent, rc, style,
                 std::make_unique<MapOverlayInvisibleButtonRenderer>());
}

bool
ShowAirspaceToggleButton::OnClicked() noexcept
{
  InputEvents::eventAirSpace("toggle");
  return true;
}

void
ShowPanToggleButton::Create(ContainerWindow &parent,
                            [[maybe_unused]] const ButtonLook &look,
                            const PixelRect &rc,
                            WindowStyle style) noexcept
{
  Button::Create(parent, rc, style,
                 std::make_unique<MapOverlayInvisibleButtonRenderer>());
}

bool
ShowPanToggleButton::OnClicked() noexcept
{
  InputEvents::eventPan("toggle");
  return true;
}

void
ShowBottomAreaToggleButton::Create(ContainerWindow &parent,
                                   [[maybe_unused]] const ButtonLook &look,
                                   const PixelRect &rc,
                                   WindowStyle style) noexcept
{
  Button::Create(parent, rc, style,
                 std::make_unique<BottomAreaToggleButtonRenderer>());
}

bool
ShowBottomAreaToggleButton::OnClicked() noexcept
{
  PageActions::ToggleBottomArea();
  return true;
}

void
ShowZoomButton::Create(ContainerWindow &parent, const ButtonLook &look,
                       const PixelRect &rc, Sign _sign,
                       WindowStyle style) noexcept
{
  sign = _sign;
  Button::Create(parent, rc, style,
                 MakeMapOverlaySymbolButton(look,
                                          sign == Sign::ZOOM_IN ? "+" : "-"));
}

bool
ShowZoomButton::OnClicked() noexcept
{
  InputEvents::eventZoom(sign == Sign::ZOOM_IN ? "in" : "out");
  return true;
}

#ifdef ANDROID

#include "Resources.hpp"

void
ShowRotateButton::Create(ContainerWindow &parent, const PixelRect &rc,
                         WindowStyle style) noexcept
{
  bitmap.Load(IDB_ROTATE);
  Button::Create(parent, rc, style,
                 std::make_unique<BitmapButtonRenderer>(bitmap, true));
}

bool
ShowRotateButton::OnClicked() noexcept
{
  /* query the device sensor for the current physical orientation
     and rotate to it */
  if (native_view != nullptr) {
    auto orientation = static_cast<DisplayOrientation>(
      native_view->GetPhysicalOrientation(Java::GetEnv()));
    if (orientation != DisplayOrientation::DEFAULT)
      Display::Rotate(orientation);
  }

  /* hide the button immediately */
  Hide();

  return true;
}

#endif /* ANDROID */
