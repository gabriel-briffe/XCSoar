// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WaypointDialogs.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Widget/ListWidget.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "Profile/Profile.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "Waypoints/WaypointsDisplaySetting.hpp"
#include "ui/canvas/Canvas.hpp"
#include "Screen/Layout.hpp"
#include "UIGlobals.hpp"
#include "Look/DialogLook.hpp"
#include "Language/Language.hpp"

#include <cassert>

class WaypointFilterListWidget : public ListWidget {
  bool changed;
  TextRowRenderer row_renderer;

public:
  WaypointFilterListWidget() noexcept
    :changed(false) {}

  bool IsModified() const noexcept {
    return changed;
  }

  void Prepare(ContainerWindow &parent,
               const PixelRect &rc) noexcept override {
    const auto &look = UIGlobals::GetDialogLook();
    ListControl &list = CreateList(parent, look, rc,
                                   row_renderer.CalculateLayout(*look.list.font));
    list.SetLength(WaypointsDisplaySetting::FilterDialogRowCount());
  }

  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;

  bool CanActivateItem([[maybe_unused]] unsigned index) const noexcept override {
    return true;
  }

  void OnActivateItem(unsigned index) noexcept override;
};

void
WaypointFilterListWidget::OnPaintItem(Canvas &canvas, PixelRect rc,
                                      unsigned i) noexcept
{
  assert(i < WaypointsDisplaySetting::FilterDialogRowCount());

  const auto id = WaypointsDisplaySetting::FilterDialogRowId(i);
  const auto &desc = WaypointsDisplaySetting::Get(id);
  const bool display = WaypointsDisplaySetting::GetLive(id) != 0;

  rc.right = display
    ? row_renderer.DrawRightColumn(canvas, rc, _("Display"))
    : row_renderer.PreviousRightColumn(canvas, rc, _("Display"));

  row_renderer.DrawTextRow(canvas, rc,
                           PageSettingCatalog::GettextOptional(desc.label));
}

void
WaypointFilterListWidget::OnActivateItem(unsigned index) noexcept
{
  assert(index < WaypointsDisplaySetting::FilterDialogRowCount());

  const auto id = WaypointsDisplaySetting::FilterDialogRowId(index);
  PageSettingSet(id, WaypointsDisplaySetting::GetLive(id) ? 0 : 1);

  changed = true;
  GetList().Invalidate();
}

void
dlgWaypointFilterShowModal() noexcept
{
  TWidgetDialog<WaypointFilterListWidget>
    dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
           UIGlobals::GetDialogLook(),
           _("Waypoints"));
  dialog.AddButton(_("Close"), mrOK);
  dialog.SetWidget();

  dialog.ShowModal();

  if (dialog.GetWidget().IsModified())
    Profile::Save();
}
