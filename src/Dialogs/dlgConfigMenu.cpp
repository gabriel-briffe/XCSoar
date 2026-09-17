// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "dlgConfigMenu.hpp"
#include "Asset.hpp"
#include "Dialogs/Dialogs.h"
#include "Form/Button.hpp"
#include "Form/GridView.hpp"
#include "Input/InputEvents.hpp"
#include "Language/Language.hpp"
#include "Look/Colors.hpp"
#include "Look/DialogLook.hpp"
#include "Look/IconLook.hpp"
#include "Math/Util.hpp"
#include "Menu/ButtonLabel.hpp"
#include "Menu/MenuData.hpp"
#include "Renderer/ButtonRenderer.hpp"
#include "Renderer/TextRenderer.hpp"
#include "Screen/Layout.hpp"
#include "UIGlobals.hpp"
#include "Widget/WindowWidget.hpp"
#include "WidgetDialog.hpp"
#include "ui/canvas/Brush.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Icon.hpp"
#include "ui/event/KeyCode.hpp"
#include "util/StaticString.hxx"
#include "util/StringAPI.hxx"
#include "util/StringCompare.hxx"

#include <algorithm>
#include <boost/container/static_vector.hpp>
#include <cstdlib>
#include <initializer_list>
#include <memory>

namespace {

[[gnu::pure]]
static bool
IsConfigPagerOrCancel(const char *label) noexcept
{
  if (label == nullptr)
    return true;

  return StringIsEqual(label, "Cancel") ||
    strstr(label, "Page ") != nullptr;
}

/**
 * Garmin-style rounded tile: filled background, border, icon above
 * caption when provided.
 */
class ConfigMenuTileRenderer final : public ButtonRenderer {
  const DialogLook &look;
  TextRenderer text_renderer;
  const StaticString<64> caption;
  const MaskedIcon *icon;

public:
  explicit ConfigMenuTileRenderer(const DialogLook &_look,
                                  const char *_caption,
                                  const MaskedIcon *_icon=nullptr) noexcept
    :look(_look), caption(_caption), icon(_icon)
  {
    text_renderer.SetCenter();
    text_renderer.SetVCenter();
    text_renderer.SetControl();
  }

  [[gnu::pure]]
  unsigned GetMinimumButtonWidth() const noexcept override
  {
    return 2 * Layout::GetTextPadding() +
      look.button.font->TextSize(caption).width;
  }

  void DrawButton(Canvas &canvas, const PixelRect &rc,
                  ButtonState state) const noexcept override
  {
    const unsigned gap = Layout::Scale(4);
    PixelRect tile = rc;
    if (tile.GetWidth() > 2 * gap && tile.GetHeight() > 2 * gap)
      tile.Grow(-int(gap));

    const unsigned radius = Layout::Scale(12);
    const unsigned diameter = std::min(2u * radius,
                                       std::min(unsigned(tile.GetWidth()),
                                                unsigned(tile.GetHeight())));

    Color fill, border, text;

    if (IsDithered() || !HasColors()) {
      fill = COLOR_WHITE;
      border = COLOR_BLACK;
      text = COLOR_BLACK;
      if (state == ButtonState::PRESSED || state == ButtonState::FOCUSED)
        fill = COLOR_LIGHT_GRAY;
    } else if (look.dark_mode) {
      fill = COLOR_CONFIG_MENU_TILE;
      border = COLOR_CONFIG_MENU_TILE_BORDER;
      text = COLOR_WHITE;
      switch (state) {
      case ButtonState::PRESSED:
        fill = COLOR_CONFIG_MENU_TILE_PRESSED;
        break;
      case ButtonState::FOCUSED:
        fill = COLOR_CONFIG_MENU_TILE_FOCUSED;
        break;
      case ButtonState::DISABLED:
        text = look.button.disabled.color;
        break;
      case ButtonState::SELECTED:
      case ButtonState::ENABLED:
        break;
      }
    } else {
      fill = COLOR_CONFIG_MENU_TILE_LIGHT;
      border = COLOR_CONFIG_MENU_TILE_BORDER_LIGHT;
      text = COLOR_BLACK;
      switch (state) {
      case ButtonState::PRESSED:
        fill = COLOR_CONFIG_MENU_TILE_PRESSED_LIGHT;
        break;
      case ButtonState::FOCUSED:
        fill = COLOR_CONFIG_MENU_TILE_FOCUSED_LIGHT;
        text = COLOR_WHITE;
        break;
      case ButtonState::DISABLED:
        text = look.button.disabled.color;
        break;
      case ButtonState::SELECTED:
      case ButtonState::ENABLED:
        break;
      }
    }

    canvas.SelectNullPen();
    canvas.Select(Brush(border));
    canvas.DrawRoundRectangle(tile, PixelSize{diameter, diameter});

    const unsigned border_w = Layout::ScaleFinePenWidth(2);
    PixelRect inner = tile;
    if (inner.GetWidth() > 2 * border_w &&
        inner.GetHeight() > 2 * border_w) {
      inner.Grow(-int(border_w));
      const unsigned inner_diameter =
        std::min(diameter,
                 std::min(unsigned(inner.GetWidth()),
                          unsigned(inner.GetHeight())));
      canvas.Select(Brush(fill));
      canvas.DrawRoundRectangle(inner, PixelSize{inner_diameter,
                                                 inner_diameter});
    }

    canvas.Select(*look.button.font);
    canvas.SetTextColor(text);
    canvas.SetBackgroundTransparent();

    if (icon != nullptr && icon->IsDefined()) {
      const unsigned H = unsigned(inner.GetHeight());
      const unsigned W = unsigned(inner.GetWidth());
      const unsigned line_h = look.button.font->GetHeight();
      /* Allow wrapped captions (e.g. "Data Management"); cap at two
         lines so the icon still has room. */
      unsigned T = text_renderer.GetHeight(*look.button.font, W, caption);
      if (T < line_h)
        T = line_h;
      else if (T > 2 * line_h)
        T = 2 * line_h;
      unsigned I = Layout::Scale(36);

      /* Three equal gaps: above icon, between icon and text, below
         text.  Shrink the icon if needed so all three fit. */
      if (I + T + 3 > H) {
        if (H > T + 3)
          I = H - T - 3;
        else
          I = 0;
      }

      if (I > 0) {
        const unsigned rem = H - I - T;
        const unsigned gap = rem / 3;
        const unsigned rem_extra = rem % 3;
        const unsigned gap_top = gap + (rem_extra > 0 ? 1u : 0u);
        const unsigned gap_mid = gap + (rem_extra > 1 ? 1u : 0u);
        /* gap_bot = gap (+ leftover absorbed by integer division) */

        const PixelPoint icon_pt{
          (inner.left + inner.right) / 2,
          inner.top + int(gap_top + I / 2),
        };
        icon->Draw(canvas, icon_pt, I);

        PixelRect text_rc = inner;
        text_rc.top = inner.top + int(gap_top + I + gap_mid);
        text_rc.bottom = text_rc.top + int(T);
        text_renderer.Draw(canvas, text_rc, caption);
      } else {
        text_renderer.Draw(canvas, tile, caption);
      }
    } else {
      text_renderer.Draw(canvas, tile, caption);
    }
  }
};

[[gnu::pure]]
static const MaskedIcon *
ConfigMenuIconForLabel(const char *label) noexcept
{
  if (label == nullptr)
    return nullptr;

  const IconLook &icons = UIGlobals::GetIconLook();
  if (StringIsEqual(label, "Planes"))
    return &icons.hBmpConfigPlanes;
  if (StringIsEqual(label, "Configuration"))
    return &icons.hBmpTabSettings;
  if (StringIsEqual(label, "Tools"))
    return &icons.hBmpTabWrench;
  if (StringIsEqual(label, "Profiles"))
    return &icons.hBmpConfigProfiles;
  if (StringIsEqual(label, "Flight Setup"))
    return &icons.hBmpConfigFlightSetup;
  if (StringIsEqual(label, "Devices"))
    return &icons.hBmpTabSystem;
  if (StringIsEqual(label, "Wind"))
    return &icons.hBmpConfigWind;
  if (StringIsEqual(label, "Data Management"))
    return &icons.hBmpConfigDataManagement;
  if (StringIsEqual(label, "Waypoint Editor"))
    return &icons.hBmpConfigWaypointEditor;
  if (StringStartsWith(label, "Replay"))
    return &icons.hBmpConfigReplay;
  if (StringStartsWith(label, "Logger\n") ||
      StringIsEqual(label, "Logger"))
    return &icons.hBmpConfigLoggerStart;
  if (StringIsEqual(label, "Lua"))
    return &icons.hBmpConfigLua;
  if (StringIsEqual(label, "WeGlide Upload"))
    return &icons.hBmpConfigUpload;

  return nullptr;
}

class ConfigMenu final : public WindowWidget {
  WndForm &dialog;
  const Menu &items;
  const char *title;

  boost::container::static_vector<Button, GridView::MAX_ITEMS> buttons;

  unsigned column_width = 0;
  unsigned row_height = 0;

  Button *previous_button = nullptr;
  Button *next_button = nullptr;

  void ApplyGridGeometry(const PixelRect &rc) noexcept;

public:
  unsigned clicked_event = 0;

  ConfigMenu(WndForm &_dialog, const Menu &_items,
             const char *_title) noexcept
    :dialog(_dialog), items(_items), title(_title) {}

  auto &GetWindow() noexcept {
    return (GridView &)WindowWidget::GetWindow();
  }

  void NavigatePage(GridView::Direction direction) noexcept;
  void UpdateCaption() noexcept;
  void SetNavigationButtons(Button *prev, Button *next) noexcept {
    previous_button = prev;
    next_button = next;
  }

  bool Focus() noexcept {
    return SetFocus();
  }

  bool IsWindowReady() const noexcept {
    return IsDefined();
  }

protected:
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  void Show(const PixelRect &rc) noexcept override;
  void Move(const PixelRect &rc) noexcept override;
  bool SetFocus() noexcept override;
  bool KeyPress(unsigned key_code) noexcept override;
};

void
ConfigMenu::ApplyGridGeometry(const PixelRect &rc) noexcept
{
  const bool portrait = rc.GetHeight() >= rc.GetWidth();
  const unsigned cols = portrait ? 3u : 4u;
  const unsigned rows = portrait ? 4u : 3u;
  column_width = std::max(1u, rc.GetWidth() / cols);
  row_height = std::max(1u, rc.GetHeight() / rows);
}

void
ConfigMenu::Prepare(ContainerWindow &parent,
                    [[maybe_unused]] const PixelRect &rc) noexcept
{
  WindowStyle grid_view_style;
  grid_view_style.ControlParent();
  grid_view_style.Hide();

  const auto &dialog_look = UIGlobals::GetDialogLook();

  PixelRect client_rc = dialog.GetClientAreaWindow().GetClientRect();
  const unsigned titlebar_height = dialog_look.caption.font->GetHeight();
  if (client_rc.GetHeight() > titlebar_height)
    client_rc.bottom = client_rc.top +
      (client_rc.GetHeight() - titlebar_height);

  ApplyGridGeometry(client_rc);

  auto grid_view = std::make_unique<GridView>();
  grid_view->Create(parent, dialog_look, client_rc, grid_view_style,
                    column_width, row_height);

  WindowStyle button_style;
  button_style.TabStop();

  PixelRect button_rc{0, 0, Layout::Scale(80), Layout::Scale(30)};

  for (unsigned i = 0; i < Menu::MAX_ITEMS; ++i) {
    if (buttons.size() >= buttons.max_size())
      break;

    const auto &menu_item = items[i];
    if (!menu_item.IsDefined())
      continue;
    if (IsConfigPagerOrCancel(menu_item.label))
      continue;

    char buffer[100];
    const auto expanded =
      ButtonLabel::Expand(menu_item.label, std::span{buffer});
    if (!expanded.visible)
      continue;

    auto &button = buttons.emplace_back(
      *grid_view, button_rc, button_style,
      std::make_unique<ConfigMenuTileRenderer>(
        dialog_look, expanded.text,
        ConfigMenuIconForLabel(menu_item.label)),
      [this, &menu_item]() {
        clicked_event = menu_item.event;
        dialog.SetModalResult(mrOK);
      });
    button.SetEnabled(expanded.enabled);
    grid_view->AddItem(button);
  }

  grid_view->RefreshLayout();
  SetWindow(std::move(grid_view));
  UpdateCaption();
}

void
ConfigMenu::NavigatePage(GridView::Direction direction) noexcept
{
  if (!IsWindowReady())
    return;

  auto &grid_view = GetWindow();
  grid_view.RefreshLayout();
  grid_view.ShowNextPage(direction);
  Focus();
  UpdateCaption();
}

void
ConfigMenu::Show(const PixelRect &rc) noexcept
{
  ApplyGridGeometry(rc);
  WindowWidget::Show(rc);

  auto &grid_view = GetWindow();
  grid_view.SetColumnWidth(column_width);
  grid_view.SetRowHeight(row_height);
  grid_view.RefreshLayout();
  UpdateCaption();
}

void
ConfigMenu::Move(const PixelRect &rc) noexcept
{
  ApplyGridGeometry(rc);
  WindowWidget::Move(rc);

  auto &grid_view = GetWindow();
  grid_view.SetColumnWidth(column_width);
  grid_view.SetRowHeight(row_height);
  grid_view.RefreshLayout();
  UpdateCaption();
}

void
ConfigMenu::UpdateCaption() noexcept
{
  auto &grid_view = GetWindow();
  StaticString<32> buffer;
  const unsigned page_size =
    grid_view.GetNumColumns() * grid_view.GetNumRows();
  const unsigned last_page =
    std::max(1u, DivideRoundUp(unsigned(buttons.size()), page_size));
  const unsigned current_page =
    std::min(grid_view.GetCurrentPage(), last_page - 1u);

  if (last_page > 1)
    buffer.Format("%s  %u/%u", gettext(title),
                  current_page + 1, last_page);
  else
    buffer = gettext(title);

  dialog.SetCaption(buffer);

  if (previous_button != nullptr)
    previous_button->SetEnabled(last_page > 1);
  if (next_button != nullptr)
    next_button->SetEnabled(last_page > 1);
}

bool
ConfigMenu::SetFocus() noexcept
{
  auto &grid_view = GetWindow();
  grid_view.RefreshLayout();

  if (buttons.empty())
    return false;

  const unsigned page_size =
    std::max(1u, grid_view.GetNumColumns() * grid_view.GetNumRows());
  const unsigned last_page =
    DivideRoundUp(unsigned(buttons.size()), page_size) - 1u;
  const unsigned current_page =
    std::min(grid_view.GetCurrentPage(), last_page);
  const unsigned page_start = current_page * page_size;
  const unsigned page_end =
    std::min(page_start + page_size, unsigned(buttons.size()));

  for (unsigned i = page_start; i < page_end; ++i) {
    if (buttons[i].IsVisible() && buttons[i].IsEnabled() &&
        buttons[i].IsTabStop()) {
      buttons[i].SetFocus();
      return true;
    }
  }

  return false;
}

bool
ConfigMenu::KeyPress(unsigned key_code) noexcept
{
  auto &grid_view = GetWindow();

  switch (key_code) {
  case KEY_LEFT:
    grid_view.MoveFocus(GridView::Direction::LEFT);
    break;
  case KEY_RIGHT:
    grid_view.MoveFocus(GridView::Direction::RIGHT);
    break;
  case KEY_UP:
    grid_view.MoveFocus(GridView::Direction::UP);
    break;
  case KEY_DOWN:
    grid_view.MoveFocus(GridView::Direction::DOWN);
    break;
  case KEY_MENU:
    grid_view.ShowNextPage();
    UpdateCaption();
    break;
  default:
    return false;
  }

  return true;
}

class ConfigMenuDialog final : public WidgetDialog {
  ConfigMenu *config_menu_widget = nullptr;

public:
  ConfigMenuDialog(Full, UI::SingleWindow &parent, const DialogLook &look,
                   const char *caption) noexcept
    :WidgetDialog(Full{}, parent, look, caption) {}

  template<typename... Args>
  void SetWidget(Args &&...args)
  {
    auto widget = std::make_unique<ConfigMenu>(std::forward<Args>(args)...);
    config_menu_widget = widget.get();
    FinishPreliminary(std::move(widget));
  }

  ConfigMenu &GetWidget() noexcept {
    return *config_menu_widget;
  }

  int ShowModal()
  {
    if (IsAutoSize())
      AutoSize();
    else
      widget.Move(buttons.BottomLayout());

    widget.Show();
    int result = WndForm::ShowModal();
    widget.Hide();
    return result;
  }

protected:
  void OnResize(PixelSize new_size) noexcept override {
    WndForm::OnResize(new_size);
    if (IsAutoSize())
      return;
    widget.Move(buttons.BottomLayout());
  }
};

/**
 * Flatten menu items from the given InputEvents modes into one Menu,
 * dropping page-nav and Cancel entries.
 */
static void
CollectMenuItems(Menu &out, std::initializer_list<const char *> modes) noexcept
{
  out.Clear();
  unsigned dest = 0;

  for (const char *mode : modes) {
    const Menu *menu = InputEvents::GetMenu(mode);
    if (menu == nullptr)
      continue;

    for (unsigned i = 0; i < Menu::MAX_ITEMS; ++i) {
      const auto &item = (*menu)[i];
      if (!item.IsDefined())
        continue;
      if (IsConfigPagerOrCancel(item.label))
        continue;
      if (dest >= Menu::MAX_ITEMS)
        return;
      out.Add(item.label, dest++, item.event);
    }
  }
}

static int
ShowConfigMenu(UI::SingleWindow &parent, const Menu &menu,
               const char *title) noexcept
{
  const auto &dialog_look = UIGlobals::GetDialogLook();

  ConfigMenuDialog dialog(WidgetDialog::Full{}, parent, dialog_look, nullptr);
  dialog.SetWidget(dialog, menu, title);
  dialog.PrepareWidget();

  auto &config_menu = dialog.GetWidget();
  Button *prev_button = dialog.AddSymbolButton("<", [&config_menu]() {
    config_menu.NavigatePage(GridView::Direction::LEFT);
  });
  Button *next_button = dialog.AddSymbolButton(">", [&config_menu]() {
    config_menu.NavigatePage(GridView::Direction::RIGHT);
  });
  dialog.AddButton(_("Close"), mrCancel);

  config_menu.SetNavigationButtons(prev_button, next_button);
  config_menu.UpdateCaption();

  if (dialog.ShowModal() != mrOK)
    return -1;

  return int(dialog.GetWidget().clicked_event);
}

static void
ShowConfigMenuAndRun(UI::SingleWindow &parent,
                     std::initializer_list<const char *> modes,
                     const char *title) noexcept
{
  Menu items;
  CollectMenuItems(items, modes);
  if (!items[0].IsDefined())
    return;

  const int event = ShowConfigMenu(parent, items, title);
  if (event >= 0)
    InputEvents::ProcessEvent(unsigned(event));
}

} // namespace

void
dlgConfigMenuShowModal(UI::SingleWindow &parent) noexcept
{
  ShowConfigMenuAndRun(parent, {"Config1", "Config2", "Config3"},
                       N_("Config"));
}

void
dlgConfigToolsShowModal(UI::SingleWindow &parent) noexcept
{
  ShowConfigMenuAndRun(parent, {"ConfigTools"}, N_("Tools"));
}
