// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TopographyDialogs.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Widget/ListWidget.hpp"
#include "Profile/Profile.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "ui/canvas/Canvas.hpp"
#include "UIGlobals.hpp"
#include "Look/DialogLook.hpp"
#include "Language/Language.hpp"
#include "ActionInterface.hpp"
#include "Components.hpp"
#include "DataComponents.hpp"
#include "Topography/TopographyStore.hpp"
#include "Topography/TopographyFile.hpp"
#include "Topography/TopographySettings.hpp"

#include <cassert>
#include <vector>

class TopologyFilterListWidget : public ListWidget {
  bool changed = false;
  TextRowRenderer row_renderer;
  std::vector<TopographyFile *> layers;

public:
  bool IsModified() const noexcept {
    return changed;
  }

  void Prepare(ContainerWindow &parent,
               const PixelRect &rc) noexcept override {
    if (data_components != nullptr)
      if (TopographyStore *store = data_components->topography.get())
        for (auto &file : *store)
          layers.push_back(&file);

    const auto &look = UIGlobals::GetDialogLook();
    ListControl &list = CreateList(parent, look, rc,
                                   row_renderer.CalculateLayout(*look.list.font));
    list.SetLength(layers.size());
  }

  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;

  bool CanActivateItem([[maybe_unused]] unsigned index) const noexcept override {
    return true;
  }

  void OnActivateItem(unsigned index) noexcept override;
};

void
TopologyFilterListWidget::OnPaintItem(Canvas &canvas, PixelRect rc,
                                      unsigned i) noexcept
{
  assert(i < layers.size());
  const TopographyFile &file = *layers[i];

  rc.right = file.ShowsShapes()
    ? row_renderer.DrawRightColumn(canvas, rc, _("Graphic"))
    : row_renderer.PreviousRightColumn(canvas, rc, _("Graphic"));

  if (file.HasLabels())
    rc.right = file.ShowsLabels()
      ? row_renderer.DrawRightColumn(canvas, rc, _("Label"))
      : row_renderer.PreviousRightColumn(canvas, rc, _("Label"));

  row_renderer.DrawTextRow(canvas, rc, file.GetLayerName());
}

void
TopologyFilterListWidget::OnActivateItem(unsigned index) noexcept
{
  assert(index < layers.size());

  TopographyFile &file = *layers[index];
  file.CycleDisplayMode();

  if (data_components != nullptr)
    if (TopographyStore *store = data_components->topography.get())
      store->NotifyThresholdsChanged();

  changed = true;
  ActionInterface::SendMapSettings(true);
  GetList().Invalidate();
}

void
dlgTopologyFilterShowModal() noexcept
{
  TWidgetDialog<TopologyFilterListWidget>
    dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
           UIGlobals::GetDialogLook(),
           _("Topology"));
  dialog.AddButton(_("Close"), mrOK);
  dialog.SetWidget();

  dialog.ShowModal();

  if (dialog.GetWidget().IsModified()) {
    if (data_components != nullptr)
      if (TopographyStore *store = data_components->topography.get())
        TopographySettings::SaveFromStore(*store);
    Profile::Save();
  }
}
