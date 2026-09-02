// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettingDescriptor.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "Widget/ListWidget.hpp"

class Canvas;
class ContainerWindow;
struct PixelRect;

/**
 * List of page-setting filters (label + right-hand state columns).
 */
class PageSettingFilterListWidget : public ListWidget {
public:
  using RowCountFn = unsigned (*)() noexcept;
  using RowIdFn = PageSettingId (*)(unsigned row) noexcept;
  using GetLiveFn = int (*)(PageSettingId id) noexcept;
  using PaintColumnsFn = void (*)(Canvas &canvas, PixelRect &rc,
                                  TextRowRenderer &row_renderer,
                                  int value) noexcept;
  using ActivateFn = void (*)(PageSettingId id, int live_value,
                              bool &changed) noexcept;

  PageSettingFilterListWidget(RowCountFn row_count, RowIdFn row_id,
                              GetLiveFn get_live,
                              PaintColumnsFn paint_columns,
                              ActivateFn activate) noexcept;

  [[nodiscard]]
  bool
  IsModified() const noexcept
  {
    return changed;
  }

  void Prepare(ContainerWindow &parent,
               const PixelRect &rc) noexcept override;
  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;
  bool CanActivateItem(unsigned index) const noexcept override;
  void OnActivateItem(unsigned index) noexcept override;

private:
  RowCountFn row_count;
  RowIdFn row_id;
  GetLiveFn get_live;
  PaintColumnsFn paint_columns;
  ActivateFn activate;
  TextRowRenderer row_renderer;
  bool changed = false;
};

namespace PageSettingFilterList {

void
PaintBoolDisplayColumn(Canvas &canvas, PixelRect &rc,
                       TextRowRenderer &row_renderer, int value) noexcept;

void
ActivateBoolDisplayToggle(PageSettingId id, int live_value,
                          bool &changed) noexcept;

void
PaintAirspaceClassFilterColumns(Canvas &canvas, PixelRect &rc,
                                TextRowRenderer &row_renderer,
                                int mode) noexcept;

void
ActivateAirspaceClassFilter(PageSettingId id, int live_value,
                            bool &changed) noexcept;

} // namespace PageSettingFilterList
