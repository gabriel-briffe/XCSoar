// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Airspace.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Widget/ListWidget.hpp"
#include "Profile/Profile.hpp"
#include "Airspace/AirspaceClassFilterProfile.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "ui/canvas/Canvas.hpp"
#include "Screen/Layout.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "MainWindow.hpp"
#include "UIGlobals.hpp"
#include "Look/Look.hpp"
#include "Airspace/AirspaceClass.hpp"
#include "Renderer/AirspacePreviewRenderer.hpp"
#include "Formatter/AirspaceFormatter.hpp"
#include "Interface.hpp"
#include "ActionInterface.hpp"
#include "Language/Language.hpp"

#include <cassert>

static_assert(OTHER == 0,
              "Airspace settings list skips OTHER as the first enum value");

[[nodiscard]]
static int
NextClassFilterMode(int mode) noexcept
{
  ++mode;
  if (mode > int(AirspaceClassFilterMode::WARN_AND_DISPLAY))
    return int(AirspaceClassFilterMode::NONE);
  return mode;
}

class AirspaceSettingsListWidget : public ListWidget {
  const bool color_mode;
  bool changed;

  TextRowRenderer row_renderer;

public:
  AirspaceSettingsListWidget(bool _color_mode)
    :color_mode(_color_mode), changed(false) {}

  bool IsModified() const {
    return changed;
  }

  void Prepare(ContainerWindow &parent,
               const PixelRect &rc) noexcept override {
    const auto &look = UIGlobals::GetDialogLook();
    ListControl &list = CreateList(parent, look, rc,
                                   row_renderer.CalculateLayout(*look.list.font));
    if (color_mode) {
      /* Skip OTHER ("Unknown"): empty AY uses GetTypeOrClass() so its
         warn/display/colour settings have no effect (#1772). */
      list.SetLength(AIRSPACECLASSCOUNT - 1);
    } else
      list.SetLength(AirspaceDisplaySetting::FilterDialogRowCount());
  }

  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;

  bool CanActivateItem([[maybe_unused]] unsigned index) const noexcept override {
    return true;
  }

  void OnActivateItem(unsigned index) noexcept override;
};

void
AirspaceSettingsListWidget::OnPaintItem(Canvas &canvas, PixelRect rc,
                                        unsigned i) noexcept
{
  if (color_mode) {
    assert(i + 1 < AIRSPACECLASSCOUNT);
    const AirspaceClass type = AirspaceClass(i + 1);

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
    return;
  }

  assert(i < AirspaceDisplaySetting::FilterDialogRowCount());
  const auto id = AirspaceDisplaySetting::FilterDialogRowId(i);
  const auto &desc = AirspaceDisplaySetting::Get(id);
  const int mode = AirspaceDisplaySetting::GetLive(id);

  rc.right = AirspaceClassFilterProfile::Display(mode)
    ? row_renderer.DrawRightColumn(canvas, rc, _("Display"))
    : row_renderer.PreviousRightColumn(canvas, rc, _("Display"));

  rc.right = AirspaceClassFilterProfile::Warn(mode)
    ? row_renderer.DrawRightColumn(canvas, rc, _("Warn"))
    : row_renderer.PreviousRightColumn(canvas, rc, _("Warn"));

  row_renderer.DrawTextRow(canvas, rc,
                           PageSettingCatalog::GettextOptional(desc.label));
}

void
AirspaceSettingsListWidget::OnActivateItem(unsigned index) noexcept
{
  if (color_mode) {
    assert(index + 1 < AIRSPACECLASSCOUNT);
    const AirspaceClass type = AirspaceClass(index + 1);

    AirspaceLook &look =
      CommonInterface::main_window->SetLook().map.airspace;
    const AirspaceRendererSettings &renderer =
      CommonInterface::GetMapSettings().airspace;

    if (!ShowAirspaceClassRendererSettingsDialog(type))
      return;

    ActionInterface::SendMapSettings();
    look.Reinitialise(renderer);
  } else {
    assert(index < AirspaceDisplaySetting::FilterDialogRowCount());
    const auto id = AirspaceDisplaySetting::FilterDialogRowId(index);
    const int mode = AirspaceDisplaySetting::GetLive(id);
    PageSettingSet(id, NextClassFilterMode(mode));
    changed = true;
  }

  GetList().Invalidate();
}

void
dlgAirspaceShowModal(bool color_mode)
{
  TWidgetDialog<AirspaceSettingsListWidget>
    dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
           UIGlobals::GetDialogLook(),
           _("Airspace"));
  dialog.AddButton(_("Close"), mrOK);
  dialog.SetWidget(color_mode);

  dialog.ShowModal();

  if (dialog.GetWidget().IsModified())
    Profile::Save();
}
