// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageOnlyCommandsWidget.hpp"

#include "Dialogs/ComboPicker.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Form/Button.hpp"
#include "Form/DataField/ComboList.hpp"
#include "Language/Language.hpp"
#include "Look/DialogLook.hpp"
#include "PageSettingCommand.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "UIGlobals.hpp"
#include "Widget/ListWidget.hpp"
#include "ui/canvas/Canvas.hpp"
#include "util/StaticArray.hxx"

#include <cassert>
#include <memory>

class PageOnlyCommandsWidget final : public ListWidget {
  PageOnlyCommands &commands;
  Button *add_button = nullptr;
  Button *delete_button = nullptr;
  TextRowRenderer row_renderer;

  StaticArray<PageSettingId, PageOnlyCommands::MAX_ITEMS> rows;

  void RebuildRows() noexcept;
  void UpdateActionButtons() noexcept;
  void OnAddClicked() noexcept;
  void OnDeleteClicked() noexcept;

public:
  explicit PageOnlyCommandsWidget(PageOnlyCommands &_commands) noexcept
    :commands(_commands) {}

  void SetActionButtons(Button &_add, Button &_delete) noexcept {
    add_button = &_add;
    delete_button = &_delete;
    UpdateActionButtons();
  }

  void AddClicked() noexcept {
    OnAddClicked();
  }

  void DeleteClicked() noexcept {
    OnDeleteClicked();
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;
  void OnCursorMoved(unsigned index) noexcept override;
  bool CanActivateItem(unsigned index) const noexcept override;
  void OnActivateItem(unsigned index) noexcept override;
};

void
PageOnlyCommandsWidget::RebuildRows() noexcept
{
  rows.clear();
  for (unsigned i = 0; i < commands.n_items; ++i)
    rows.append(commands.ids[i]);

  if (IsDefined()) {
    GetList().SetLength(rows.size());
    GetList().Invalidate();
  }
  UpdateActionButtons();
}

void
PageOnlyCommandsWidget::UpdateActionButtons() noexcept
{
  if (add_button != nullptr)
    add_button->SetEnabled(commands.n_items < PageSettingCommandCount() &&
                           commands.n_items < PageOnlyCommands::MAX_ITEMS);
  if (delete_button != nullptr)
    delete_button->SetEnabled(IsDefined() &&
                              GetList().GetLength() > 0);
}

void
PageOnlyCommandsWidget::OnAddClicked() noexcept
{
  ComboList list;
  StaticArray<PageSettingId, PageOnlyCommands::MAX_ITEMS> ids;

  for (unsigned i = 0; i < PageSettingCommandCount(); ++i) {
    const auto &cmd = PageSettingCommandGet(i);
    if (commands.Contains(cmd.id))
      continue;
    list.Append(ids.size(), gettext(cmd.label));
    ids.append(cmd.id);
  }

  if (ids.empty())
    return;

  int result = ComboPicker(_("Add"), list, nullptr);
  if (result < 0 || unsigned(result) >= ids.size())
    return;

  commands.Add(ids[unsigned(result)]);
  RebuildRows();
}

void
PageOnlyCommandsWidget::OnDeleteClicked() noexcept
{
  if (!IsDefined() || GetList().GetLength() == 0)
    return;

  const unsigned index = GetList().GetCursorIndex();
  if (index >= rows.size())
    return;

  commands.Remove(rows[index]);
  RebuildRows();
}

void
PageOnlyCommandsWidget::Prepare(ContainerWindow &parent,
                                const PixelRect &rc) noexcept
{
  const DialogLook &look = UIGlobals::GetDialogLook();
  ListControl &list = CreateList(parent, look, rc,
                                 row_renderer.CalculateLayout(*look.list.font));
  RebuildRows();
  list.SetLength(rows.size());
  UpdateActionButtons();
}

void
PageOnlyCommandsWidget::OnPaintItem(Canvas &canvas, const PixelRect rc,
                                    unsigned idx) noexcept
{
  assert(idx < rows.size());

  const auto *cmd = PageSettingCommandFind(rows[idx]);
  assert(cmd != nullptr);
  row_renderer.DrawTextRow(canvas, rc, gettext(cmd->label));
}

void
PageOnlyCommandsWidget::OnCursorMoved(unsigned) noexcept
{
  UpdateActionButtons();
}

bool
PageOnlyCommandsWidget::CanActivateItem(unsigned) const noexcept
{
  return false;
}

void
PageOnlyCommandsWidget::OnActivateItem(unsigned) noexcept
{
}

void
ShowPageOnlyCommandsDialog(PageOnlyCommands &commands) noexcept
{
  const DialogLook &look = UIGlobals::GetDialogLook();
  WidgetDialog dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
                      look, _("Distinct commands"));

  auto list = std::make_unique<PageOnlyCommandsWidget>(commands);
  auto &widget = *list;

  dialog.FinishPreliminary(std::move(list));
  Button *add = dialog.AddButton(_("Add"), [&widget](){
    widget.AddClicked();
  });
  Button *del = dialog.AddButton(_("Delete"), [&widget](){
    widget.DeleteClicked();
  });
  widget.SetActionButtons(*add, *del);
  dialog.AddButton(_("Close"), mrOK);
  dialog.ShowModal();
}
