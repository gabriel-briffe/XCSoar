// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Airspace.hpp"
#include "Dialogs/Settings/PageSettingFilterListWidget.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Widget/ListWidget.hpp"
#include "Profile/Profile.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "ui/canvas/Canvas.hpp"
#include "Screen/Layout.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "MainWindow.hpp"
#include "UIGlobals.hpp"
#include "Look/Look.hpp"
#include "Renderer/AirspacePreviewRenderer.hpp"
#include "Formatter/AirspaceFormatter.hpp"
#include "Interface.hpp"
#include "ActionInterface.hpp"
#include "Language/Language.hpp"

#include <cassert>

class AirspaceColorListWidget : public ListWidget {
  TextRowRenderer row_renderer;

public:
  void Prepare(ContainerWindow &parent,
               const PixelRect &rc) noexcept override {
    const auto &look = UIGlobals::GetDialogLook();
    ListControl &list = CreateList(parent, look, rc,
                                   row_renderer.CalculateLayout(*look.list.font));
    list.SetLength(AirspaceDisplaySetting::FilterDialogRowCount());
  }

  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;

  bool CanActivateItem(unsigned) const noexcept override {
    return true;
  }

  void OnActivateItem(unsigned index) noexcept override;
};

void
AirspaceColorListWidget::OnPaintItem(Canvas &canvas, PixelRect rc,
                                     unsigned i) noexcept
{
  assert(i < AirspaceDisplaySetting::FilterDialogRowCount());

  const PageSettingId id = AirspaceDisplaySetting::FilterDialogRowId(i);
  const AirspaceClass type = AirspaceDisplaySetting::ClassFromFilterId(id);

  const AirspaceRendererSettings &renderer =
    CommonInterface::GetMapSettings().airspace;
  const AirspaceLook &look =
    CommonInterface::main_window->GetLook().map.airspace;

  const char *const name = AirspaceFormatter::GetClass(type);

  int second_x = row_renderer.NextColumn(canvas, rc, name);

  const int padding = Layout::GetTextPadding();

  const Color text_color = canvas.GetTextColor();
  if (AirspacePreviewRenderer::PrepareFill(
      canvas, type, look, renderer)) {
    canvas.DrawRectangle({second_x, rc.top + padding, rc.right - padding,
                          rc.bottom - padding});
  }
  AirspacePreviewRenderer::UnprepareFill(canvas, text_color);
  if (AirspacePreviewRenderer::PrepareOutline(
      canvas, type, look, renderer)) {
    canvas.DrawRectangle({second_x, rc.top + padding, rc.right - padding,
                          rc.bottom - padding});
  }

  row_renderer.DrawTextRow(canvas, rc, name);
}

void
AirspaceColorListWidget::OnActivateItem(unsigned index) noexcept
{
  assert(index < AirspaceDisplaySetting::FilterDialogRowCount());

  const PageSettingId id = AirspaceDisplaySetting::FilterDialogRowId(index);
  const AirspaceClass type = AirspaceDisplaySetting::ClassFromFilterId(id);

  AirspaceLook &look =
    CommonInterface::main_window->SetLook().map.airspace;
  const AirspaceRendererSettings &renderer =
    CommonInterface::GetMapSettings().airspace;

  if (!ShowAirspaceClassRendererSettingsDialog(type))
    return;

  ActionInterface::SendMapSettings();
  look.Reinitialise(renderer);
  GetList().Invalidate();
}

void
dlgAirspaceShowModal(bool color_mode)
{
  if (color_mode) {
    TWidgetDialog<AirspaceColorListWidget>
      dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
             UIGlobals::GetDialogLook(),
             _("Airspace"));
    dialog.AddButton(_("Close"), mrOK);
    dialog.SetWidget();
    dialog.ShowModal();
    return;
  }

  TWidgetDialog<PageSettingFilterListWidget>
    dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
           UIGlobals::GetDialogLook(),
           _("Airspace"));
  dialog.AddButton(_("Close"), mrOK);
  dialog.SetWidget(AirspaceDisplaySetting::FilterDialogRowCount,
                   AirspaceDisplaySetting::FilterDialogRowId,
                   AirspaceDisplaySetting::GetLive,
                   PageSettingFilterList::PaintAirspaceClassFilterColumns,
                   PageSettingFilterList::ActivateAirspaceClassFilter);

  dialog.ShowModal();

  if (dialog.GetWidget().IsModified())
    Profile::Save();
}
