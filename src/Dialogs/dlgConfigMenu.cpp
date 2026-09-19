// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "dlgConfigMenu.hpp"
#include "Asset.hpp"
#include "Dialogs/Dialogs.h"
#include "Dialogs/Message.hpp"
#include "Form/Button.hpp"
#include "Form/Form.hpp"
#include "Form/Frame.hpp"
#include "Form/GridView.hpp"
#include "Input/InputEvents.hpp"
#include "Language/Language.hpp"
#include "Look/Colors.hpp"
#include "Look/DialogLook.hpp"
#include "Look/IconLook.hpp"
#include "Math/Util.hpp"
#include "Menu/ButtonLabel.hpp"
#include "Menu/MenuData.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
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
#include "ui/window/SingleWindow.hpp"
#include "util/IterableSplitString.hxx"
#include "util/StaticString.hxx"
#include "util/StringAPI.hxx"
#include "util/StringCompare.hxx"

#include <algorithm>
#include <boost/container/static_vector.hpp>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

namespace {

/** Modal result: open the Hidden folder of this tiled menu. */
constexpr int mrOpenHidden = 100;

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
 * Yes / Cancel confirmation (no No button).
 */
static bool
ConfirmYesCancel(const char *text, const char *caption) noexcept
{
  auto &main_window = UIGlobals::GetMainWindow();
  const auto main_rc = main_window.GetClientRect();
  const auto &dialog_look = UIGlobals::GetDialogLook();

  PixelSize client_area_size(Layout::Scale(200u), Layout::Scale(160u));
  const auto button_size = Layout::Scale(PixelSize{60u, 32u});

  WindowStyle style;
  style.Hide();
  style.ControlParent();

  WndForm wf(main_window, dialog_look, PixelRect{client_area_size},
             caption, style);
  ContainerWindow &client_area = wf.GetClientAreaWindow();

  WndFrame text_frame(client_area, dialog_look,
                      client_area.GetClientRect());
  text_frame.SetText(text);
  text_frame.SetAlignCenter();

  const unsigned text_height = text_frame.GetTextHeight();
  text_frame.Resize({
      client_area_size.width,
      text_height + Layout::GetTextPadding(),
    });

  client_area_size.height = Layout::Scale(10) + text_height +
    button_size.height;

  const auto dialog_size = wf.ClientAreaToDialogSize(client_area_size);
  const auto dialog_position = main_rc.CenteredTopLeft(dialog_size);
  wf.Move(PixelRect{dialog_position, dialog_size});

  const PixelRect button_rc(PixelPoint(0, Layout::Scale(6u) + text_height),
                            button_size);

  WindowStyle button_style;
  button_style.TabStop();

  Button yes_button(client_area, dialog_look.button, _("Yes"), button_rc,
                    button_style, wf.MakeModalResultCallback(IDYES));
  Button cancel_button(client_area, dialog_look.button, _("Cancel"),
                       button_rc, button_style,
                       wf.MakeModalResultCallback(IDCANCEL));

  const unsigned max_button_width = client_area_size.width / 2;
  yes_button.Move({int(max_button_width / 2 - button_size.width / 2),
                   button_rc.top});
  cancel_button.Move({int(max_button_width + max_button_width / 2 -
                          button_size.width / 2),
                      button_rc.top});

  return wf.ShowModal() == IDYES;
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
  /** False when the menu macro disabled the action (e.g. Vega). */
  const bool action_enabled;

public:
  explicit ConfigMenuTileRenderer(const DialogLook &_look,
                                  const char *_caption,
                                  const MaskedIcon *_icon=nullptr,
                                  bool _action_enabled=true) noexcept
    :look(_look), caption(_caption), icon(_icon),
     action_enabled(_action_enabled)
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
    /* Keep the window enabled so long-press still works; draw as
       disabled when the underlying menu action is unavailable. */
    if (!action_enabled && state != ButtonState::PRESSED)
      state = ButtonState::DISABLED;

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

/** Invisible placeholder used to pad the Hidden tile to bottom-right. */
class EmptyTileRenderer final : public ButtonRenderer {
public:
  [[gnu::pure]]
  unsigned GetMinimumButtonWidth() const noexcept override {
    return 0;
  }

  void DrawButton(Canvas &, const PixelRect &,
                  ButtonState) const noexcept override {}
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
  if (StringIsEqual(label, "Hidden"))
    return &icons.hBmpConfigHidden;

  return nullptr;
}

static void
FlattenCaption(StaticString<64> &dest, const char *caption) noexcept
{
  dest.clear();
  for (const char *p = caption; *p != '\0' && !dest.full(); ++p) {
    if (*p == '\n') {
      if (!dest.empty() && dest.back() != ' ')
        dest.push_back(' ');
    } else
      dest.push_back(*p);
  }
}

static std::string
MakeHiddenProfileKey(const char *menu_id) noexcept
{
  std::string key{ProfileKeys::TiledMenuHiddenPrefix};
  key += menu_id;
  return key;
}

class TiledMenu final : public WindowWidget {
  WndForm &dialog;
  const Menu &items;
  const char *title;
  const char *menu_id;
  const bool showing_hidden;

  boost::container::static_vector<Button, GridView::MAX_ITEMS> buttons;

  unsigned column_width = 0;
  unsigned row_height = 0;

  Button *previous_button = nullptr;
  Button *next_button = nullptr;

  boost::container::static_vector<StaticString<64>, Menu::MAX_ITEMS>
    hidden_labels;

  void ApplyGridGeometry(const PixelRect &rc) noexcept;
  void LoadHiddenLabels() noexcept;
  void SaveHiddenLabels() noexcept;
  [[gnu::pure]]
  bool IsHiddenLabel(const char *label) const noexcept;
  void AddHiddenLabel(const char *label) noexcept;
  void RemoveHiddenLabel(const char *label) noexcept;
  void ClearTiles() noexcept;
  void PopulateTiles() noexcept;
  void RebuildTiles() noexcept;
  void OnHideTile(const char *label, const char *caption) noexcept;
  void OnUnhideTile(const char *label, const char *caption) noexcept;

public:
  unsigned clicked_event = 0;

  TiledMenu(WndForm &_dialog, const Menu &_items,
            const char *_title, const char *_menu_id,
            bool _showing_hidden) noexcept
    :dialog(_dialog), items(_items), title(_title), menu_id(_menu_id),
     showing_hidden(_showing_hidden)
  {
    LoadHiddenLabels();
  }

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
TiledMenu::ApplyGridGeometry(const PixelRect &rc) noexcept
{
  const bool portrait = rc.GetHeight() >= rc.GetWidth();
  const unsigned cols = portrait ? 3u : 4u;
  const unsigned rows = portrait ? 4u : 3u;
  column_width = std::max(1u, rc.GetWidth() / cols);
  row_height = std::max(1u, rc.GetHeight() / rows);
}

void
TiledMenu::LoadHiddenLabels() noexcept
{
  hidden_labels.clear();

  const std::string key = MakeHiddenProfileKey(menu_id);
  const char *value = Profile::Get(key, "");
  if (value == nullptr || *value == '\0')
    return;

  for (const auto token : IterableSplitString(value, '|')) {
    if (token.empty() || hidden_labels.size() >= hidden_labels.max_size())
      continue;

    auto &dest = hidden_labels.emplace_back();
    for (char c : token) {
      if (dest.full())
        break;
      dest.push_back(c == '\x1e' ? '\n' : c);
    }
  }
}

void
TiledMenu::SaveHiddenLabels() noexcept
{
  std::string value;
  for (const auto &label : hidden_labels) {
    if (!value.empty())
      value.push_back('|');
    for (const char *p = label.c_str(); *p != '\0'; ++p)
      value.push_back(*p == '\n' ? '\x1e' : *p);
  }

  const std::string key = MakeHiddenProfileKey(menu_id);
  Profile::Set(key, value.c_str());
  Profile::Save();
}

bool
TiledMenu::IsHiddenLabel(const char *label) const noexcept
{
  if (label == nullptr)
    return false;

  for (const auto &hidden : hidden_labels)
    if (StringIsEqual(hidden.c_str(), label))
      return true;
  return false;
}

void
TiledMenu::AddHiddenLabel(const char *label) noexcept
{
  if (label == nullptr || IsHiddenLabel(label))
    return;
  if (hidden_labels.size() >= hidden_labels.max_size())
    return;

  hidden_labels.emplace_back(label);
  SaveHiddenLabels();
}

void
TiledMenu::RemoveHiddenLabel(const char *label) noexcept
{
  if (label == nullptr)
    return;

  for (auto it = hidden_labels.begin(); it != hidden_labels.end(); ++it) {
    if (StringIsEqual(it->c_str(), label)) {
      hidden_labels.erase(it);
      SaveHiddenLabels();
      return;
    }
  }
}

void
TiledMenu::ClearTiles() noexcept
{
  if (!IsWindowReady())
    return;

  GetWindow().ClearItems();
  buttons.clear();
}

void
TiledMenu::OnHideTile(const char *label, const char *caption) noexcept
{
  StaticString<64> flat;
  FlattenCaption(flat, caption);

  StaticString<128> prompt;
  prompt.Format(_("Hide %s?"), flat.c_str());
  if (!ConfirmYesCancel(prompt, _("Hidden")))
    return;

  AddHiddenLabel(label);
  RebuildTiles();
}

void
TiledMenu::OnUnhideTile(const char *label, const char *caption) noexcept
{
  StaticString<64> flat;
  FlattenCaption(flat, caption);

  StaticString<128> prompt;
  prompt.Format(_("Unhide %s?"), flat.c_str());
  if (!ConfirmYesCancel(prompt, _("Hidden")))
    return;

  RemoveHiddenLabel(label);

  if (hidden_labels.empty()) {
    dialog.SetModalResult(mrCancel);
    return;
  }

  RebuildTiles();
}

void
TiledMenu::PopulateTiles() noexcept
{
  auto &grid_view = GetWindow();
  const auto &dialog_look = UIGlobals::GetDialogLook();

  WindowStyle button_style;
  button_style.TabStop();

  WindowStyle spacer_style;
  spacer_style.Hide();
  spacer_style.Disable();

  PixelRect button_rc{0, 0, Layout::Scale(80), Layout::Scale(30)};

  unsigned visible_count = 0;

  for (unsigned i = 0; i < Menu::MAX_ITEMS; ++i) {
    if (buttons.size() >= buttons.max_size())
      break;

    const auto &menu_item = items[i];
    if (!menu_item.IsDefined())
      continue;
    if (IsConfigPagerOrCancel(menu_item.label))
      continue;

    const bool item_hidden = IsHiddenLabel(menu_item.label);
    if (showing_hidden != item_hidden)
      continue;

    char buffer[100];
    const auto expanded =
      ButtonLabel::Expand(menu_item.label, std::span{buffer});
    if (!expanded.visible)
      continue;

    const char *raw_label = menu_item.label;
    StaticString<64> caption_copy{expanded.text};
    const bool action_enabled = expanded.enabled;

    auto &button = buttons.emplace_back(
      grid_view, button_rc, button_style,
      std::make_unique<ConfigMenuTileRenderer>(
        dialog_look, expanded.text,
        ConfigMenuIconForLabel(menu_item.label),
        action_enabled),
      [this, &menu_item, action_enabled]() {
        /* Macro-disabled tiles (e.g. Vega) stay clickable for
           long-press hide, but a short press does nothing. */
        if (!action_enabled)
          return;
        clicked_event = menu_item.event;
        dialog.SetModalResult(mrOK);
      });

    if (showing_hidden) {
      button.SetLongPressCallback(
        [this, raw_label, caption_copy]() {
          OnUnhideTile(raw_label, caption_copy.c_str());
        });
    } else {
      button.SetLongPressCallback(
        [this, raw_label, caption_copy]() {
          OnHideTile(raw_label, caption_copy.c_str());
        });
    }

    grid_view.AddItem(button);
    ++visible_count;
  }

  /* Hidden folder tile: only on the root tiled menu, and only when
     at least one item is hidden.  Always alone on its own page at
     bottom-right. */
  if (!showing_hidden && !hidden_labels.empty() &&
      buttons.size() < buttons.max_size()) {
    const unsigned page_size =
      std::max(1u, grid_view.GetNumColumns() * grid_view.GetNumRows());

    /* Pad to the end of the last content page, then to bottom-right
       of a dedicated following page. */
    unsigned slots_used = visible_count;
    if (slots_used == 0) {
      /* Only the Hidden tile: still put it bottom-right of page 0. */
    } else if (slots_used % page_size != 0)
      slots_used = DivideRoundUp(slots_used, page_size) * page_size;

    const unsigned hidden_index = slots_used + page_size - 1;

    while (buttons.size() < hidden_index &&
           buttons.size() < buttons.max_size()) {
      auto &spacer = buttons.emplace_back(
        grid_view, button_rc, spacer_style,
        std::make_unique<EmptyTileRenderer>(),
        []() {});
      spacer.SetEnabled(false);
      grid_view.AddItem(spacer);
    }

    if (buttons.size() < buttons.max_size()) {
      auto &hidden_button = buttons.emplace_back(
        grid_view, button_rc, button_style,
        std::make_unique<ConfigMenuTileRenderer>(
          dialog_look, _("Hidden"),
          ConfigMenuIconForLabel("Hidden")),
        [this]() {
          dialog.SetModalResult(mrOpenHidden);
        });
      grid_view.AddItem(hidden_button);
    }
  }
}

void
TiledMenu::RebuildTiles() noexcept
{
  if (!IsWindowReady())
    return;

  ClearTiles();
  /* Refresh with an empty grid so column/row counts match the
     current geometry before padding the Hidden tile. */
  GetWindow().RefreshLayout();
  PopulateTiles();
  GetWindow().RefreshLayout();
  UpdateCaption();
  Focus();
}

void
TiledMenu::Prepare(ContainerWindow &parent,
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

  /* NumColumns/Rows are computed in RefreshLayout; seed them so
     PopulateTiles can pad the Hidden tile correctly. */
  SetWindow(std::move(grid_view));
  GetWindow().RefreshLayout();
  PopulateTiles();
  GetWindow().RefreshLayout();
  UpdateCaption();
}

void
TiledMenu::NavigatePage(GridView::Direction direction) noexcept
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
TiledMenu::Show(const PixelRect &rc) noexcept
{
  ApplyGridGeometry(rc);
  WindowWidget::Show(rc);

  auto &grid_view = GetWindow();
  grid_view.SetColumnWidth(column_width);
  grid_view.SetRowHeight(row_height);
  RebuildTiles();
}

void
TiledMenu::Move(const PixelRect &rc) noexcept
{
  ApplyGridGeometry(rc);
  WindowWidget::Move(rc);

  auto &grid_view = GetWindow();
  grid_view.SetColumnWidth(column_width);
  grid_view.SetRowHeight(row_height);
  RebuildTiles();
}

void
TiledMenu::UpdateCaption() noexcept
{
  auto &grid_view = GetWindow();
  StaticString<32> buffer;
  const unsigned page_size =
    std::max(1u, grid_view.GetNumColumns() * grid_view.GetNumRows());
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
TiledMenu::SetFocus() noexcept
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
TiledMenu::KeyPress(unsigned key_code) noexcept
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

class TiledMenuDialog final : public WidgetDialog {
  TiledMenu *tiled_menu_widget = nullptr;

public:
  TiledMenuDialog(Full, UI::SingleWindow &parent, const DialogLook &look,
                  const char *caption) noexcept
    :WidgetDialog(Full{}, parent, look, caption) {}

  template<typename... Args>
  void SetWidget(Args &&...args)
  {
    auto widget = std::make_unique<TiledMenu>(std::forward<Args>(args)...);
    tiled_menu_widget = widget.get();
    FinishPreliminary(std::move(widget));
  }

  TiledMenu &GetWidget() noexcept {
    return *tiled_menu_widget;
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

/**
 * Show one tiled-menu dialog.  Returns the chosen event id, -1 on
 * cancel, or a negative sentinel when the Hidden folder was opened
 * (caller should check mrOpenHidden via the modal result path).
 *
 * Actually returns: event (>=0), -1 cancel, or we need open_hidden
 * flag.  Use out parameter for open_hidden.
 */
static int
ShowTiledMenuDialog(UI::SingleWindow &parent, const Menu &menu,
                    const char *title, const char *menu_id,
                    bool showing_hidden,
                    bool &open_hidden) noexcept
{
  open_hidden = false;
  const auto &dialog_look = UIGlobals::GetDialogLook();

  TiledMenuDialog dialog(WidgetDialog::Full{}, parent, dialog_look, nullptr);
  dialog.SetWidget(dialog, menu, title, menu_id, showing_hidden);
  dialog.PrepareWidget();

  auto &tiled_menu = dialog.GetWidget();
  Button *prev_button = dialog.AddSymbolButton("<", [&tiled_menu]() {
    tiled_menu.NavigatePage(GridView::Direction::LEFT);
  });
  Button *next_button = dialog.AddSymbolButton(">", [&tiled_menu]() {
    tiled_menu.NavigatePage(GridView::Direction::RIGHT);
  });
  dialog.AddButton(_("Close"), mrCancel);

  tiled_menu.SetNavigationButtons(prev_button, next_button);
  tiled_menu.UpdateCaption();

  const int result = dialog.ShowModal();
  if (result == mrOpenHidden) {
    open_hidden = true;
    return -1;
  }
  if (result != mrOK)
    return -1;

  return int(dialog.GetWidget().clicked_event);
}

static void
ShowTiledMenuAndRun(UI::SingleWindow &parent,
                    std::initializer_list<const char *> modes,
                    const char *title,
                    const char *menu_id) noexcept
{
  Menu items;
  CollectMenuItems(items, modes);
  if (!items[0].IsDefined())
    return;

  for (;;) {
    bool open_hidden = false;
    const int event = ShowTiledMenuDialog(parent, items, title, menu_id,
                                          false, open_hidden);
    if (open_hidden) {
      bool unused = false;
      const int hidden_event =
        ShowTiledMenuDialog(parent, items, N_("Hidden"), menu_id,
                            true, unused);
      if (hidden_event >= 0) {
        InputEvents::ProcessEvent(unsigned(hidden_event));
        return;
      }
      /* Closed Hidden folder (or emptied it): refresh root menu. */
      continue;
    }

    if (event >= 0)
      InputEvents::ProcessEvent(unsigned(event));
    return;
  }
}

} // namespace

void
dlgConfigMenuShowModal(UI::SingleWindow &parent) noexcept
{
  ShowTiledMenuAndRun(parent, {"Config1", "Config2", "Config3"},
                      N_("Config"), "Config");
}

void
dlgConfigToolsShowModal(UI::SingleWindow &parent) noexcept
{
  ShowTiledMenuAndRun(parent, {"ConfigTools"}, N_("Tools"), "Tools");
}
