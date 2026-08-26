// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RainbowDialog.hpp"
#include "Weather/Features.hpp"

#ifdef HAVE_HTTP

#include "WeatherOverlayDraft.hpp"
#include "UIGlobals.hpp"
#include "Form/ButtonPanel.hpp"
#include "Form/CheckBox.hpp"
#include "Look/DialogLook.hpp"
#include "Renderer/TwoTextRowsRenderer.hpp"
#include "Screen/Layout.hpp"
#include "Widget/MultiSelectListWidget.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Widget/TwoWidgets.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "Interface.hpp"
#include "UIState.hpp"
#include "Language/Language.hpp"
#include "PageSettings.hpp"
#include "ui/canvas/Canvas.hpp"

#include <memory>

namespace {

enum LayerRow {
  ROW_SATELLITE = 0,
  ROW_RAIN = 1,
  ROW_COUNT = 2,
};

class RainbowOptionsPanel;

class RainbowLayerListWidget final : public MultiSelectListWidget {
  TwoTextRowsRenderer row_renderer;
  RainbowOptionsPanel *options_panel = nullptr;

public:
  void SetOptionsPanel(RainbowOptionsPanel *panel) noexcept {
    options_panel = panel;
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    const auto &look = UIGlobals::GetDialogLook();
    CreateList(parent, look, rc,
               row_renderer.CalculateLayout(*look.list.font_bold,
                                            look.small_font));
    GetList().SetActivateOnFirstClick(true);

    const auto &settings =
      CommonInterface::GetComputerSettings().weather.rainbow;
    SetLengthWithSelection(ROW_COUNT);
    SetSelected(ROW_SATELLITE, settings.display_satellite);
    SetSelected(ROW_RAIN, settings.display_rain);
  }

  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned i) noexcept override;

  void PersistSelection() noexcept;

protected:
  void OnSelectionChanged() noexcept override;
};

class RainbowOptionsPanel final : public RowFormWidget {
  WeatherOverlayDraft::State overlay;
  Button *add_page_button = nullptr;
  RainbowLayerListWidget *list = nullptr;

public:
  RainbowOptionsPanel() noexcept
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void SetList(RainbowLayerListWidget *_list) noexcept {
    list = _list;
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  void Show(const PixelRect &rc) noexcept override;
  void SyncDraftFromList() noexcept;
  void RefreshButtons() noexcept;

  /** Push checkbox layers onto the current Rainbow page, if any. */
  void ApplyLayersToCurrentRainbowPage() noexcept;

private:
  void AddPageClicked() noexcept;
};

void
RainbowLayerListWidget::OnPaintItem(Canvas &canvas, const PixelRect rc,
                                    unsigned i) noexcept
{
  const DialogLook &look = UIGlobals::GetDialogLook();
  const bool focused = GetList().HasFocus();
  const unsigned padding = Layout::GetTextPadding();
  const unsigned box_size = rc.GetHeight() > 2 * padding
    ? rc.GetHeight() - 2 * padding
    : 0;

  PixelRect box_rc;
  box_rc.left = rc.left + int(padding);
  box_rc.top = rc.top + int(padding);
  box_rc.right = box_rc.left + int(box_size);
  box_rc.bottom = box_rc.top + int(box_size);

  DrawCheckBox(canvas, look, box_rc, IsSelected(i), focused, false, true);

  const char *title = i == ROW_SATELLITE
    ? C_("Weather layer", "Satellite")
    : C_("Weather layer", "Rain");
  const char *detail = i == ROW_SATELLITE
    ? _("Latest Rainbow cloud tiles")
    : _("Latest Rainbow precipitation tiles");

  PixelRect text_rc = rc;
  text_rc.left = box_rc.right + 2 * int(padding);
  row_renderer.DrawFirstRow(canvas, text_rc, title);
  row_renderer.DrawSecondRow(canvas, text_rc, detail);
}

void
RainbowLayerListWidget::PersistSelection() noexcept
{
  auto &settings = CommonInterface::SetComputerSettings().weather.rainbow;
  settings.display_satellite = IsSelected(ROW_SATELLITE);
  settings.display_rain = IsSelected(ROW_RAIN);
  Profile::Set(ProfileKeys::RainbowDisplaySatellite,
               settings.display_satellite);
  Profile::Set(ProfileKeys::RainbowDisplayRain, settings.display_rain);
}

void
RainbowLayerListWidget::OnSelectionChanged() noexcept
{
  /* Keep at least one layer so the Rainbow page stays valid. */
  if (!IsSelected(ROW_SATELLITE) && !IsSelected(ROW_RAIN)) {
    SetSelected(ROW_RAIN, true);
    return;
  }

  PersistSelection();
  if (options_panel != nullptr) {
    options_panel->SyncDraftFromList();
    options_panel->ApplyLayersToCurrentRainbowPage();
  }
}

void
RainbowOptionsPanel::Prepare(ContainerWindow &parent,
                             const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  add_page_button = AddButton(C_("Button", "Add page"), [this]() {
    AddPageClicked();
  });
  AddButton(_("Pages setup"), []() {
    WeatherOverlayDraft::OpenPagesConfig();
  });
}

void
RainbowOptionsPanel::Show(const PixelRect &rc) noexcept
{
  RowFormWidget::Show(rc);
  overlay.Load(PageLayout::Overlay::RAINBOW);

  /* When the open page is already Rainbow, drive the checkboxes from
     that page so Sat/Rain match what is on the map. */
  const auto &pages = CommonInterface::GetUISettings().pages;
  const unsigned current =
    CommonInterface::GetUIState().pages.current_index;
  if (list != nullptr && current < pages.n_pages &&
      pages.pages[current].UsesRainbowOverlay()) {
    const auto &page = pages.pages[current];
    auto &settings =
      CommonInterface::SetComputerSettings().weather.rainbow;
    settings.display_satellite = page.rainbow_satellite;
    settings.display_rain = page.rainbow_rain;
    list->SetSelected(ROW_SATELLITE, page.rainbow_satellite);
    list->SetSelected(ROW_RAIN, page.rainbow_rain);
  }

  SyncDraftFromList();
}

void
RainbowOptionsPanel::SyncDraftFromList() noexcept
{
  if (list != nullptr)
    list->PersistSelection();

  const auto &settings =
    CommonInterface::GetComputerSettings().weather.rainbow;
  overlay.draft.overlay = PageLayout::Overlay::RAINBOW;
  overlay.draft.bottom = PageLayout::Bottom::WEATHER_CONTROLS;
  overlay.draft.rainbow_satellite = settings.display_satellite;
  overlay.draft.rainbow_rain = settings.display_rain;
  if (overlay.draft.rainbow_time < PageLayout::RAINBOW_TIME_AUTO)
    overlay.draft.rainbow_time = PageLayout::RAINBOW_TIME_AUTO;
  overlay.draft.Normalise();
  RefreshButtons();
}

void
RainbowOptionsPanel::RefreshButtons() noexcept
{
  overlay.SyncButtons(nullptr, add_page_button);
}

void
RainbowOptionsPanel::ApplyLayersToCurrentRainbowPage() noexcept
{
  const auto &pages = CommonInterface::GetUISettings().pages;
  const unsigned current =
    CommonInterface::GetUIState().pages.current_index;
  if (current >= pages.n_pages ||
      !pages.pages[current].UsesRainbowOverlay())
    return;

  /* Without "Apply to page", layer checkboxes still update the open
     Rainbow page immediately (Sat/Rain only — time stays on the page). */
  if (overlay.ApplyIfDirty())
    RefreshButtons();
}

void
RainbowOptionsPanel::AddPageClicked() noexcept
{
  SyncDraftFromList();
  overlay.AddPage(nullptr, add_page_button);
}

} // namespace

std::unique_ptr<Widget>
CreateRainbowWidget()
{
  auto list = std::make_unique<RainbowLayerListWidget>();
  auto options = std::make_unique<RainbowOptionsPanel>();
  auto *list_ptr = list.get();
  auto *options_ptr = options.get();
  list_ptr->SetOptionsPanel(options_ptr);
  options_ptr->SetList(list_ptr);

  return std::make_unique<TwoWidgets>(std::move(list),
                                      std::move(options),
                                      true);
}

#else

std::unique_ptr<Widget>
CreateRainbowWidget()
{
  return nullptr;
}

#endif
