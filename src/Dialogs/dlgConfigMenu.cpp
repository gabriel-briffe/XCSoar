// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "dlgConfigMenu.hpp"
#include "Asset.hpp"
#include "Dialogs/InternalLink.hpp"
#include "Dialogs/Message.hpp"
#include "UIActions.hpp"
#include "Dialogs/Settings/ConfigMenuData.hpp"
#include "Dialogs/Settings/Panels/SiteConfigPanel.hpp"
#include "Dialogs/DataManagement/AdvancedFileExplorer.hpp"
#include "Dialogs/DataManagement/BackupRestorePanel.hpp"
#include "Dialogs/DataManagement/ExportFlightsPanel.hpp"
#include "Dialogs/DataManagement/ImportDataPanel.hpp"
#include "Dialogs/FileManager.hpp"
#include "Form/Button.hpp"
#include "Form/Form.hpp"
#include "Form/Frame.hpp"
#include "Form/GridView.hpp"
#include "Form/TabMenuData.hpp"
#include "Input/InputEvents.hpp"
#include "Language/Language.hpp"
#include "Look/Colors.hpp"
#include "Look/DialogLook.hpp"
#include "Look/IconLook.hpp"
#include "Math/Util.hpp"
#include "Menu/ButtonLabel.hpp"
#include "Menu/MenuData.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
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
      unsigned T = text_renderer.GetHeight(*look.button.font, W, caption);
      if (T < line_h)
        T = line_h;
      else if (T > 2 * line_h)
        T = 2 * line_h;
      unsigned I = Layout::Scale(36);

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
  if (StringIsEqual(label, "Flight Display"))
    return &icons.hBmpConfigDisplay;
  if (StringIsEqual(label, "Pages"))
    return &icons.hBmpConfigPages;
  if (StringIsEqual(label, "Display"))
    return &icons.hBmpConfigDisplay;
  if (StringIsEqual(label, "Sounds"))
    return &icons.hBmpConfigSounds;
  if (StringIsEqual(label, "Language"))
    return &icons.hBmpConfigLanguage;
  if (StringIsEqual(label, "Quit"))
    return &icons.hBmpConfigQuit;
  if (StringIsEqual(label, "System Setup"))
    return &icons.hBmpTabSettings;
  if (StringIsEqual(label, "Glide Computer"))
    return &icons.hBmpConfigGlideComputer;
  if (StringIsEqual(label, "Settings"))
    return &icons.hBmpConfigSettingsWrench;
  if (StringIsEqual(label, "Safety Factors"))
    return &icons.hBmpConfigSafetyFactors;
  if (StringIsEqual(label, "Task"))
    return &icons.hBmpTabTask;
  if (StringIsEqual(label, "Task Defaults"))
    return &icons.hBmpTabRules;
  if (StringIsEqual(label, "Map"))
    return &icons.hBmpConfigMap;
  if (StringIsEqual(label, "Gauges"))
    return &icons.hBmpConfigGauges;
  if (StringIsEqual(label, "InfoBoxes"))
    return &icons.hBmpConfigInfoBoxes;
  if (StringIsEqual(label, "Orientation"))
    return &icons.hBmpConfigOrientation;
  if (StringIsEqual(label, "Waypoints"))
    return &icons.hBmpConfigWaypoints;
  if (StringIsEqual(label, "Airspace"))
    return &icons.hBmpConfigAirspace;
  if (StringIsEqual(label, "Aircraft"))
    return &icons.hBmpConfigAircraft;
  if (StringIsEqual(label, "Aircrafts"))
    return &icons.hBmpConfigAircrafts;
  if (StringIsEqual(label, "Traffic"))
    return &icons.hBmpConfigTraffic;
  if (StringIsEqual(label, "Terrain"))
    return &icons.hBmpConfigTerrain;
  if (StringIsEqual(label, "Topology"))
    return &icons.hBmpConfigTopology;
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
  if (StringIsEqual(label, "Weather"))
    return &icons.hBmpConfigWeather;
  if (StringStartsWith(label, "Distance Rings"))
    return &icons.hBmpConfigDistanceRings;
  if (StringIsEqual(label, "Data"))
    return &icons.hBmpConfigDataManagement;
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

/**
 * One tile in a tiled menu.  EVENT closes the menu and returns an
 * InputEvents id; NESTED / PANEL / ACTION open a child UI without
 * closing.
 */
struct TiledMenuItem {
  StaticString<64> id;
  StaticString<64> caption;
  const MaskedIcon *icon = nullptr;
  bool enabled = true;

  enum class Kind : uint8_t {
    EVENT,
    NESTED,
    PANEL,
    ACTION,
  } kind = Kind::EVENT;

  unsigned event = 0;
  void (*show_nested)(UI::SingleWindow &parent) noexcept = nullptr;
  std::unique_ptr<Widget> (*create_panel)() = nullptr;
  void (*show_action)() noexcept = nullptr;
};

using TiledMenuItemList =
  boost::container::static_vector<TiledMenuItem, GridView::MAX_ITEMS>;

static void ShowFlightDisplayTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowMapTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowAircraftsTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowGaugesTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowInfoBoxesTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowGlideComputerTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowTaskTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowTaskDefaultsTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowDataTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowSystemSetupTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowHardwareTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowLookAccessibilityTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowUnitsTimeTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowAccountsServicesTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowWeatherTiledMenu(UI::SingleWindow &parent) noexcept;
static void ShowTiledMenuList(UI::SingleWindow &parent,
                              const char *title,
                              const char *menu_id,
                              TiledMenuItemList items) noexcept;

/** Nesting depth of ShowTiledMenuList (1 = root Config/Tools/Data). */
static unsigned tiled_menu_depth = 0;

/** When true, nested menus cancel themselves to return to the map. */
static bool tiled_menu_exit_all = false;

/**
 * Quit was confirmed from a tiled-menu tile.  #MainWindow::OnClose
 * only cancels the open dialog while one is up, so we defer
 * #UIActions::SignalShutdown until the Config menu has closed.
 */
static bool quit_after_tiled_menu = false;

class TiledMenu final : public WindowWidget {
  WndForm &dialog;
  TiledMenuItemList &items;
  const char *title;
  const char *menu_id;
  const bool showing_hidden;
  UI::SingleWindow &parent_window;

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
  void ActivateItem(const TiledMenuItem &item) noexcept;

public:
  unsigned clicked_event = 0;

  TiledMenu(WndForm &_dialog, TiledMenuItemList &_items,
            const char *_title, const char *_menu_id,
            bool _showing_hidden,
            UI::SingleWindow &_parent_window) noexcept
    :dialog(_dialog), items(_items), title(_title), menu_id(_menu_id),
     showing_hidden(_showing_hidden), parent_window(_parent_window)
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
TiledMenu::ActivateItem(const TiledMenuItem &item) noexcept
{
  switch (item.kind) {
  case TiledMenuItem::Kind::EVENT:
    if (!item.enabled)
      return;
    clicked_event = item.event;
    dialog.SetModalResult(mrOK);
    break;

  case TiledMenuItem::Kind::NESTED:
    if (item.show_nested != nullptr) {
      item.show_nested(parent_window);
      if (tiled_menu_exit_all)
        dialog.SetModalResult(mrCancel);
    }
    break;

  case TiledMenuItem::Kind::PANEL:
    if (item.create_panel != nullptr) {
      ShowConfigPanel(item.caption.c_str(), item.create_panel);
      if (tiled_menu_exit_all)
        dialog.SetModalResult(mrCancel);
    }
    break;

  case TiledMenuItem::Kind::ACTION:
    if (item.show_action != nullptr) {
      item.show_action();
      if (tiled_menu_exit_all)
        dialog.SetModalResult(mrCancel);
    }
    break;
  }
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

  for (unsigned i = 0; i < items.size(); ++i) {
    if (buttons.size() >= buttons.max_size())
      break;

    const TiledMenuItem &item = items[i];
    const bool item_hidden = IsHiddenLabel(item.id.c_str());
    if (showing_hidden != item_hidden)
      continue;

    StaticString<64> id_copy{item.id.c_str()};
    StaticString<64> caption_copy{item.caption.c_str()};
    const unsigned item_index = i;

    auto &button = buttons.emplace_back(
      grid_view, button_rc, button_style,
      std::make_unique<ConfigMenuTileRenderer>(
        dialog_look, item.caption.c_str(), item.icon, item.enabled),
      [this, item_index]() {
        ActivateItem(items[item_index]);
      });

    if (showing_hidden) {
      button.SetLongPressCallback(
        [this, id_copy, caption_copy]() {
          OnUnhideTile(id_copy.c_str(), caption_copy.c_str());
        });
    } else {
      button.SetLongPressCallback(
        [this, id_copy, caption_copy]() {
          OnHideTile(id_copy.c_str(), caption_copy.c_str());
        });
    }

    grid_view.AddItem(button);
    ++visible_count;
  }

  /* Hidden folder tile: only on the root of a tiled menu, and only
     when at least one item is hidden.  Alone on its own page at
     bottom-right. */
  if (!showing_hidden && !hidden_labels.empty() &&
      buttons.size() < buttons.max_size()) {
    const unsigned page_size =
      std::max(1u, grid_view.GetNumColumns() * grid_view.GetNumRows());

    unsigned slots_used = visible_count;
    if (slots_used > 0 && slots_used % page_size != 0)
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
  grid_view->Create(parent, client_rc, grid_view_style,
                    column_width, row_height);

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

static int
ShowTiledMenuDialog(UI::SingleWindow &parent, TiledMenuItemList &items,
                    const char *title, const char *menu_id,
                    bool showing_hidden,
                    bool &open_hidden) noexcept
{
  open_hidden = false;
  const auto &dialog_look = UIGlobals::GetDialogLook();

  TiledMenuDialog dialog(WidgetDialog::Full{}, parent, dialog_look, nullptr);
  dialog.SetWidget(dialog, items, title, menu_id, showing_hidden, parent);
  dialog.PrepareWidget();

  auto &tiled_menu = dialog.GetWidget();
  Button *prev_button = dialog.AddSymbolButton("<", [&tiled_menu]() {
    tiled_menu.NavigatePage(GridView::Direction::LEFT);
  });
  Button *next_button = dialog.AddSymbolButton(">", [&tiled_menu]() {
    tiled_menu.NavigatePage(GridView::Direction::RIGHT);
  });

  /* Close: a short press returns one level (to the parent menu, or
     to the map from a root menu, same as the former Back button); a
     long press dismisses the whole stack back to the map. */
  Button *close_button = dialog.AddButton(_("Close"), mrCancel);
  close_button->SetLongPressCallback([&dialog]() {
    tiled_menu_exit_all = true;
    dialog.SetModalResult(mrCancel);
  });

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
ShowTiledMenuList(UI::SingleWindow &parent,
                  const char *title,
                  const char *menu_id,
                  TiledMenuItemList items) noexcept
{
  if (items.empty())
    return;

  ++tiled_menu_depth;

  for (;;) {
    if (tiled_menu_exit_all)
      break;

    bool open_hidden = false;
    const int event = ShowTiledMenuDialog(parent, items, title, menu_id,
                                          false, open_hidden);
    if (tiled_menu_exit_all)
      break;

    if (open_hidden) {
      bool unused = false;
      const int hidden_event =
        ShowTiledMenuDialog(parent, items, N_("Hidden"), menu_id,
                            true, unused);
      if (tiled_menu_exit_all)
        break;
      if (hidden_event >= 0) {
        InputEvents::ProcessEvent(unsigned(hidden_event));
        --tiled_menu_depth;
        return;
      }
      continue;
    }

    if (event >= 0)
      InputEvents::ProcessEvent(unsigned(event));
    break;
  }

  --tiled_menu_depth;
}

static void
AppendPanelPages(TiledMenuItemList &out, const TabMenuPage *pages) noexcept
{
  for (const TabMenuPage *page = pages;
       page != nullptr && page->menu_caption != nullptr; ++page) {
    if (page->available != nullptr && !page->available())
      continue;
    if (out.size() >= out.max_size())
      return;

    TiledMenuItem item;
    item.id = page->menu_caption;
    item.caption = gettext(page->menu_caption);
    item.icon = ConfigMenuIconForLabel(page->menu_caption);
    item.kind = TiledMenuItem::Kind::PANEL;
    item.create_panel = page->Load;
    out.push_back(std::move(item));
  }
}

static void
AppendNestedFolder(TiledMenuItemList &out, const char *id,
                   const char *caption,
                   void (*show_nested)(UI::SingleWindow &) noexcept) noexcept
{
  if (out.size() >= out.max_size())
    return;

  TiledMenuItem item;
  item.id = id;
  item.caption = caption;
  item.icon = ConfigMenuIconForLabel(id);
  item.kind = TiledMenuItem::Kind::NESTED;
  item.show_nested = show_nested;
  out.push_back(std::move(item));
}

static void
AppendItemsFromMenu(TiledMenuItemList &out, const Menu &menu) noexcept
{
  for (unsigned i = 0; i < Menu::MAX_ITEMS; ++i) {
    const auto &menu_item = menu[i];
    if (!menu_item.IsDefined())
      continue;
    if (IsConfigPagerOrCancel(menu_item.label))
      continue;
    if (out.size() >= out.max_size())
      return;

    char buffer[100];
    const auto expanded =
      ButtonLabel::Expand(menu_item.label, std::span{buffer});
    if (!expanded.visible)
      continue;

    TiledMenuItem item;
    item.id = menu_item.label;
    item.caption = expanded.text;
    item.icon = ConfigMenuIconForLabel(menu_item.label);
    item.enabled = expanded.enabled;
    item.kind = TiledMenuItem::Kind::EVENT;
    item.event = menu_item.event;
    out.push_back(std::move(item));
  }
}

static void
CollectXciItems(TiledMenuItemList &out,
                std::initializer_list<const char *> modes) noexcept
{
  for (const char *mode : modes) {
    const Menu *menu = InputEvents::GetMenu(mode);
    if (menu == nullptr)
      continue;

    AppendItemsFromMenu(out, *menu);
  }
}

/**
 * Move the first XCI item whose id equals #id (or starts with #id when
 * #prefix is true) from #xci into #out.  Returns false if not found.
 */
static bool
TakeXciItem(TiledMenuItemList &xci, TiledMenuItemList &out,
            const char *id, bool prefix=false) noexcept
{
  if (out.size() >= out.max_size())
    return false;

  for (auto it = xci.begin(); it != xci.end(); ++it) {
    const bool match = prefix
      ? StringStartsWith(it->id.c_str(), id)
      : StringIsEqual(it->id.c_str(), id);
    if (!match)
      continue;

    out.push_back(std::move(*it));
    xci.erase(it);
    return true;
  }

  return false;
}

static void ShowMapTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::map_pages);
  ShowTiledMenuList(parent, N_("Map"), "Map", std::move(items));
}

static void ShowAircraftsTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::aircrafts_pages);
  ShowTiledMenuList(parent, N_("Aircrafts"), "Aircrafts",
                    std::move(items));
}

static void ShowGaugesTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::gauge_pages);
  ShowTiledMenuList(parent, N_("Gauges"), "Gauges", std::move(items));
}

static void ShowInfoBoxesTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::infoboxes_pages);
  ShowTiledMenuList(parent, N_("InfoBoxes"), "InfoBoxes",
                    std::move(items));
}

static void ShowFlightDisplayTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendNestedFolder(items, "Map", _("Map"), ShowMapTiledMenu);
  AppendNestedFolder(items, "Aircrafts", _("Aircrafts"),
                     ShowAircraftsTiledMenu);
  AppendNestedFolder(items, "Gauges", _("Gauges"), ShowGaugesTiledMenu);
  AppendNestedFolder(items, "InfoBoxes", _("InfoBoxes"),
                     ShowInfoBoxesTiledMenu);
  AppendPanelPages(items, ConfigMenuData::pages_pages);
  ShowTiledMenuList(parent, N_("Flight Display"), "Flight Display",
                    std::move(items));
}

static void ShowGlideComputerTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::computer_pages);
  ShowTiledMenuList(parent, N_("Glide Computer"), "Glide Computer",
                    std::move(items));
}

static void ShowTaskDefaultsTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::task_defaults_pages);
  ShowTiledMenuList(parent, N_("Task Defaults"), "Task Defaults",
                    std::move(items));
}

static void ShowTaskTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendNestedFolder(items, "Task Defaults", _("Task Defaults"),
                     ShowTaskDefaultsTiledMenu);
  ShowTiledMenuList(parent, N_("Task"), "Task", std::move(items));
}

static void ShowHardwareTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::hardware_pages);
  ShowTiledMenuList(parent, N_("Hardware"), "Hardware", std::move(items));
}

static void
ShowLookAccessibilityTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::look_accessibility_pages);
  ShowTiledMenuList(parent, N_("Look & Accessibility"),
                    "Look & Accessibility", std::move(items));
}

static void ShowUnitsTimeTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::units_time_pages);
  ShowTiledMenuList(parent, N_("Units & Time"), "Units & Time",
                    std::move(items));
}

static void ShowWeatherTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::weather_pages);
  ShowTiledMenuList(parent, N_("Weather"), "Weather", std::move(items));
}

static void
ShowAccountsServicesTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendNestedFolder(items, "Weather", _("Weather"), ShowWeatherTiledMenu);
  AppendPanelPages(items, ConfigMenuData::accounts_pages);
  ShowTiledMenuList(parent, N_("Accounts & Services"),
                    "Accounts & Services", std::move(items));
}

static void ShowSystemSetupTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendPanelPages(items, ConfigMenuData::language_pages);
  AppendNestedFolder(items, "Hardware", _("Hardware"),
                     ShowHardwareTiledMenu);
  AppendNestedFolder(items, "Look & Accessibility",
                     _("Look & Accessibility"),
                     ShowLookAccessibilityTiledMenu);
  AppendNestedFolder(items, "Units & Time", _("Units & Time"),
                     ShowUnitsTimeTiledMenu);
  AppendPanelPages(items, ConfigMenuData::setup_pages);
  AppendNestedFolder(items, "Accounts & Services",
                     _("Accounts & Services"),
                     ShowAccountsServicesTiledMenu);
  ShowTiledMenuList(parent, N_("System Setup"), "System Setup",
                    std::move(items));
}

static void ShowToolsTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  CollectXciItems(items, {"ConfigTools"});
  ShowTiledMenuList(parent, N_("Tools"), "Tools", std::move(items));
}

static void ShowQuitAction() noexcept
{
  /* Prompt here: MainWindow::OnClose skips CheckShutdown while any
     dialog is open and would only CancelDialog the Config menu. */
  if (!UIActions::CheckShutdown())
    return;

  quit_after_tiled_menu = true;
  RequestTiledMenuCloseAll();
}

static void
ShowDataSiteFilesAction() noexcept
{
  ShowConfigPanel(_("Site Files"), CreateSiteConfigPanel);
}

static void
ShowDataDownloadManagerAction() noexcept
{
  ShowFileManager();
}

static void
ShowDataExportFlightsAction() noexcept
{
  ShowExportFlightsDialog();
}

static void
ShowDataImportDataAction() noexcept
{
  ShowImportDataDialog();
}

static void
ShowDataBackupManagerAction() noexcept
{
  ShowBackupManagerDialog();
}

static void
ShowDataAdvancedFileExplorerAction() noexcept
{
  ShowAdvancedFileExplorerDialog();
}

static void
AppendDataAction(TiledMenuItemList &out, const char *id,
                 const char *caption,
                 void (*show_action)() noexcept) noexcept
{
  if (out.size() >= out.max_size())
    return;

  TiledMenuItem item;
  item.id = id;
  item.caption = caption;
  item.icon = ConfigMenuIconForLabel(id);
  item.kind = TiledMenuItem::Kind::ACTION;
  item.show_action = show_action;
  out.push_back(std::move(item));
}

static void ShowDataTiledMenu(UI::SingleWindow &parent) noexcept
{
  TiledMenuItemList items;
  AppendDataAction(items, "Navigation & Flight Resources",
                   C_("Button", "Navigation & Flight Resources"),
                   ShowDataSiteFilesAction);
  AppendDataAction(items, "Download manager",
                   C_("Button", "Download manager"),
                   ShowDataDownloadManagerAction);
  AppendDataAction(items, "Export flights",
                   C_("Button", "Export flights"),
                   ShowDataExportFlightsAction);
  AppendDataAction(items, "Import data",
                   C_("Button", "Import data"),
                   ShowDataImportDataAction);
  AppendDataAction(items, "Backup manager",
                   C_("Button", "Backup manager"),
                   ShowDataBackupManagerAction);
  AppendDataAction(items, "Advanced File Explorer",
                   C_("Button", "Advanced File Explorer"),
                   ShowDataAdvancedFileExplorerAction);
  ShowTiledMenuList(parent, N_("Data"), "Data", std::move(items));
}

/**
 * Build the Config root tile list in fixed order.  Softkey actions
 * (Planes, Devices, …) still come from XCI Config1/2/3 so labels and
 * macros stay data-driven; folder tiles and Quit are declared here so
 * the tree is not assembled by insert-after patches.
 */
static void
BuildConfigRootItems(TiledMenuItemList &out) noexcept
{
  TiledMenuItemList xci;
  CollectXciItems(xci, {"Config1", "Config2", "Config3"});

  TakeXciItem(xci, out, "Configuration");
  AppendNestedFolder(out, "Data", _("Data"), ShowDataTiledMenu);
  AppendNestedFolder(out, "Flight Display", _("Flight Display"),
                     ShowFlightDisplayTiledMenu);
  AppendNestedFolder(out, "Glide Computer", _("Glide Computer"),
                     ShowGlideComputerTiledMenu);
  AppendNestedFolder(out, "Task", _("Task"), ShowTaskTiledMenu);
  TakeXciItem(xci, out, "Planes");
  TakeXciItem(xci, out, "Devices");
  TakeXciItem(xci, out, "Flight Setup");
  TakeXciItem(xci, out, "Wind");
  TakeXciItem(xci, out, "Profiles");
  TakeXciItem(xci, out, "Logger", true);
  /* Prefer the exact NMEA Logger label; fall back to prefix. */
  if (!TakeXciItem(xci, out, "NMEA Logger$(CheckLogger)"))
    TakeXciItem(xci, out, "NMEA Logger", true);
  TakeXciItem(xci, out, "Lua");
  TakeXciItem(xci, out, "Vega", true);
  if (TakeXciItem(xci, out, "Tools")) {
    out.back().kind = TiledMenuItem::Kind::NESTED;
    out.back().show_nested = ShowToolsTiledMenu;
  }

  /* Preserve any remaining custom Config XCI entries. */
  for (auto &item : xci) {
    if (out.size() >= out.max_size())
      break;
    out.push_back(std::move(item));
  }

  AppendNestedFolder(out, "System Setup", _("System Setup"),
                     ShowSystemSetupTiledMenu);

  if (out.size() < out.max_size()) {
    TiledMenuItem quit;
    quit.id = "Quit";
    quit.caption = _("Quit");
    quit.icon = ConfigMenuIconForLabel("Quit");
    quit.kind = TiledMenuItem::Kind::ACTION;
    quit.show_action = ShowQuitAction;
    out.push_back(std::move(quit));
  }
}

static void
ShowXciTiledMenuAndRun(UI::SingleWindow &parent,
                       std::initializer_list<const char *> modes,
                       const char *title,
                       const char *menu_id,
                       bool config_root) noexcept
{
  TiledMenuItemList items;
  if (config_root)
    BuildConfigRootItems(items);
  else
    CollectXciItems(items, modes);

  if (items.empty())
    return;

  ShowTiledMenuList(parent, title, menu_id, std::move(items));
}

} // namespace

void
dlgConfigMenuShowModal(UI::SingleWindow &parent) noexcept
{
  tiled_menu_exit_all = false;
  tiled_menu_depth = 0;
  quit_after_tiled_menu = false;
  ShowXciTiledMenuAndRun(parent, {"Config1", "Config2", "Config3"},
                         N_("Config"), "Config", true);

  if (quit_after_tiled_menu) {
    quit_after_tiled_menu = false;
    /* No dialog left: force skips a second "Quit program?" prompt. */
    UIActions::SignalShutdown(true);
  }
}

void
dlgConfigToolsShowModal(UI::SingleWindow &parent) noexcept
{
  tiled_menu_exit_all = false;
  tiled_menu_depth = 0;
  ShowXciTiledMenuAndRun(parent, {"ConfigTools"}, N_("Tools"), "Tools",
                         false);
}

void
dlgConfigDataShowModal(UI::SingleWindow &parent) noexcept
{
  tiled_menu_exit_all = false;
  tiled_menu_depth = 0;
  ShowDataTiledMenu(parent);
}

void
ShowTiledMenuFromMenu(UI::SingleWindow &parent,
                      const char *title,
                      const char *menu_id,
                      const Menu &menu) noexcept
{
  tiled_menu_exit_all = false;
  tiled_menu_depth = 0;

  TiledMenuItemList items;
  AppendItemsFromMenu(items, menu);
  if (items.empty())
    return;

  ShowTiledMenuList(parent, title, menu_id, std::move(items));
}

void
RequestTiledMenuCloseAll() noexcept
{
  tiled_menu_exit_all = true;
}
