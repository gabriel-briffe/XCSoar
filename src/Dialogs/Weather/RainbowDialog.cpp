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
#include "Weather/Rainbow/FieldControls.hpp"
#include "ui/canvas/Canvas.hpp"

#include <memory>

namespace {

enum LayerRow {
  ROW_SATELLITE = 0,
  ROW_RAIN = 1,
  ROW_SAT_RAIN = 2,
  ROW_COUNT = 3,
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
    SetSelected(ROW_SAT_RAIN, settings.display_sat_rain);
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
  void ApplyLayersToCurrentRainbowPage() noexcept;

private:
  void AddPageClicked() noexcept;
};

void
ApplyDisplayMode(PageLayout &layout, bool satellite, bool rain) noexcept
{
  layout.rainbow_satellite = satellite;
  layout.rainbow_rain = rain;
}

void
ApplyFirstEnabledMode(PageLayout &layout,
                      bool sat, bool rain, bool sat_rain) noexcept
{
  if (sat)
    ApplyDisplayMode(layout, true, false);
  else if (rain)
    ApplyDisplayMode(layout, false, true);
  else if (sat_rain)
    ApplyDisplayMode(layout, true, true);
  else
    ApplyDisplayMode(layout, false, false);
}

[[nodiscard]]
bool
ModeInPool(const PageLayout &layout,
           bool sat, bool rain, bool sat_rain) noexcept
{
  if (layout.rainbow_satellite && layout.rainbow_rain)
    return sat_rain;
  if (layout.rainbow_rain)
    return rain;
  if (layout.rainbow_satellite)
    return sat;
  return false;
}

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

  const char *title;
  const char *detail;
  switch (i) {
  case ROW_SATELLITE:
    title = C_("Weather layer", "Satellite");
    detail = _("Latest Rainbow cloud tiles");
    break;
  case ROW_RAIN:
    title = C_("Weather layer", "Rain");
    detail = _("Latest Rainbow precipitation tiles");
    break;
  case ROW_SAT_RAIN:
  default:
    title = C_("Weather layer", "Sat+Rain");
    detail = _("Rainbow satellite with rain overlay");
    break;
  }

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
  settings.display_sat_rain = IsSelected(ROW_SAT_RAIN);
  Profile::Set(ProfileKeys::RainbowDisplaySatellite,
               settings.display_satellite);
  Profile::Set(ProfileKeys::RainbowDisplayRain, settings.display_rain);
  Profile::Set(ProfileKeys::RainbowDisplaySatRain,
               settings.display_sat_rain);
}

void
RainbowLayerListWidget::OnSelectionChanged() noexcept
{
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
  if (overlay.draft.rainbow_time < PageLayout::RAINBOW_TIME_AUTO)
    overlay.draft.rainbow_time = PageLayout::RAINBOW_TIME_AUTO;

  if (!ModeInPool(overlay.draft,
                  settings.display_satellite, settings.display_rain,
                  settings.display_sat_rain))
    ApplyFirstEnabledMode(overlay.draft,
                          settings.display_satellite,
                          settings.display_rain,
                          settings.display_sat_rain);

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

  const auto &settings =
    CommonInterface::GetComputerSettings().weather.rainbow;

  auto &page = CommonInterface::SetUISettings().pages.pages[current];
  if (!ModeInPool(page,
                  settings.display_satellite, settings.display_rain,
                  settings.display_sat_rain))
    ApplyFirstEnabledMode(page,
                          settings.display_satellite,
                          settings.display_rain,
                          settings.display_sat_rain);

  page.Normalise();
  Rainbow::ApplyCursorFromPageLayout(page);
  if (page.UsesRainbowOverlay()) {
    Rainbow::PersistCursorToPage();
    Rainbow::ActivatePageOverlay();
  } else {
    Rainbow::ClearMapOverlay();
  }

  overlay.draft = page;
  overlay.draft.Normalise();
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
