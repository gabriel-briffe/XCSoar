// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSettingFilterListWidget.hpp"

#include "Airspace/AirspaceClassFilterProfile.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "Language/Language.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "UIGlobals.hpp"
#include "Look/DialogLook.hpp"
#include "ui/canvas/Canvas.hpp"

#include <cassert>

namespace PageSettingFilterList {

void
PaintBoolDisplayColumn(Canvas &canvas, PixelRect &rc,
                       TextRowRenderer &row_renderer, int value) noexcept
{
  const bool display = value != 0;
  rc.right = display
    ? row_renderer.DrawRightColumn(canvas, rc, _("Display"))
    : row_renderer.PreviousRightColumn(canvas, rc, _("Display"));
}

void
ActivateBoolDisplayToggle(PageSettingId id, int live_value,
                          bool &changed) noexcept
{
  PageSettingSet(id, live_value ? 0 : 1);
  changed = true;
}

[[nodiscard]]
static int
NextClassFilterMode(int mode) noexcept
{
  ++mode;
  if (mode > int(AirspaceClassFilterMode::WARN_AND_DISPLAY))
    return int(AirspaceClassFilterMode::NONE);
  return mode;
}

void
PaintAirspaceClassFilterColumns(Canvas &canvas, PixelRect &rc,
                                TextRowRenderer &row_renderer,
                                int mode) noexcept
{
  rc.right = AirspaceClassFilterProfile::Display(mode)
    ? row_renderer.DrawRightColumn(canvas, rc, _("Display"))
    : row_renderer.PreviousRightColumn(canvas, rc, _("Display"));

  rc.right = AirspaceClassFilterProfile::Warn(mode)
    ? row_renderer.DrawRightColumn(canvas, rc, _("Warn"))
    : row_renderer.PreviousRightColumn(canvas, rc, _("Warn"));
}

void
ActivateAirspaceClassFilter(PageSettingId id, int live_value,
                            bool &changed) noexcept
{
  PageSettingSet(id, NextClassFilterMode(live_value));
  changed = true;
}

} // namespace PageSettingFilterList

PageSettingFilterListWidget::PageSettingFilterListWidget(
  RowCountFn _row_count, RowIdFn _row_id, GetLiveFn _get_live,
  PaintColumnsFn _paint_columns, ActivateFn _activate) noexcept
  :row_count(_row_count), row_id(_row_id), get_live(_get_live),
   paint_columns(_paint_columns), activate(_activate)
{
}

void
PageSettingFilterListWidget::Prepare(ContainerWindow &parent,
                                     const PixelRect &rc) noexcept
{
  const auto &look = UIGlobals::GetDialogLook();
  ListControl &list = CreateList(parent, look, rc,
                                 row_renderer.CalculateLayout(*look.list.font));
  list.SetLength(row_count());
}

void
PageSettingFilterListWidget::OnPaintItem(Canvas &canvas, PixelRect rc,
                                         unsigned i) noexcept
{
  assert(i < row_count());

  const PageSettingId id = row_id(i);
  const auto &desc = PageSettingRegistry::Get(id);
  const int value = get_live(id);

  paint_columns(canvas, rc, row_renderer, value);

  row_renderer.DrawTextRow(canvas, rc,
                           PageSettingCatalog::GettextOptional(desc.label));
}

bool
PageSettingFilterListWidget::CanActivateItem(unsigned) const noexcept
{
  return true;
}

void
PageSettingFilterListWidget::OnActivateItem(unsigned index) noexcept
{
  assert(index < row_count());

  const PageSettingId id = row_id(index);
  activate(id, get_live(id), changed);
  GetList().Invalidate();
}
