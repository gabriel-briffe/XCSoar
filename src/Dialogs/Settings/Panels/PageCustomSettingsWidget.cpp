// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageCustomSettingsWidget.hpp"

#include "Dialogs/Airspace/Airspace.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Airspace/AirspaceClassColorProfile.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "Formatter/AirspaceFormatter.hpp"
#include "Engine/Airspace/AirspaceClass.hpp"
#include "Form/Button.hpp"
#include "Form/DataField/ComboList.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Look/AirspaceLook.hpp"
#include "Look/DialogLook.hpp"
#include "Look/Look.hpp"
#include "MainWindow.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingFilterCatalog.hpp"
#include "PageSettingModule.hpp"
#include "Renderer/AirspacePreviewRenderer.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "Screen/Layout.hpp"
#include "UIGlobals.hpp"
#include "Widget/ListWidget.hpp"
#include "ui/canvas/Canvas.hpp"
#include "util/StaticArray.hxx"
#include "util/StaticString.hxx"
#include "util/StringCompare.hxx"
#include "util/StringFormat.hpp"

#include <algorithm>
#include <cassert>
#include <memory>

class PageCustomSettingsWidget final : public ListWidget {
  PageSettingOverrides &overrides;
  Button *add_button = nullptr;
  Button *delete_button = nullptr;
  TextRowRenderer row_renderer;
  StaticArray<PageSettingId, PageSettingOverrides::MAX_ITEMS> row_ids;

  [[nodiscard]]
  static bool
  IsColorRow(PageSettingId id) noexcept
  {
    return AirspaceDisplaySetting::IsClassColor(id);
  }

  void RebuildRows() noexcept;
  void UpdateActionButtons() noexcept;
  void OnAddClicked() noexcept;
  void OnDeleteClicked() noexcept;
  void EditValueRow(unsigned index) noexcept;
  void EditColorRow(unsigned index) noexcept;

  /**
   * Merge this page's colour overrides into a copy of live map settings
   * and refresh only #AirspaceLook (same scope as the airspace colours
   * dialog — not #MainWindow::ReinitialiseLook).
   */
  void SyncAirspacePreviewLook() const noexcept;

  [[nodiscard]]
  AirspaceRendererSettings
  BuildPreviewRenderer() const noexcept;

  [[nodiscard]]
  const char *
  FormatValue(PageSettingId id, int value) const noexcept;

public:
  explicit PageCustomSettingsWidget(PageSettingOverrides &_overrides) noexcept
    :overrides(_overrides) {}

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


namespace {

[[nodiscard]]
bool
ModuleHasAddable(const PageSettingModule &module,
                 const PageSettingOverrides &overrides) noexcept
{
  for (unsigned i = 0; i < module.count(); ++i) {
    const auto &desc = module.get(PageSettingId(unsigned(module.id_start) +
                                                i));
    if (AirspaceDisplaySetting::IsClassBorderColor(desc.id) ||
        AirspaceDisplaySetting::IsClassBorderWidth(desc.id) ||
        AirspaceDisplaySetting::IsClassFillMode(desc.id))
      continue;

    if (AirspaceDisplaySetting::IsClassFillColor(desc.id)) {
      if (AirspaceDisplaySetting::HasColorOverride(
            overrides, AirspaceDisplaySetting::ClassFromFillColorId(desc.id)))
        continue;
    } else if (overrides.Contains(desc.id))
      continue;

    return true;
  }
  return false;
}

[[nodiscard]]
bool
SameSection(const char *a, const char *b) noexcept
{
  if (a == b)
    return true;
  if (a == nullptr || b == nullptr)
    return false;
  return StringIsEqual(a, b);
}

static char color_picker_labels[PageSettingAirspaceClassFillColorCount][64];
static char value_display_buffer[64];

} // namespace

void
PageCustomSettingsWidget::UpdateActionButtons() noexcept
{
  if (add_button != nullptr) {
    bool can_add = false;
    for (unsigned m = 0; m < PageSettingModuleRegistry::Count(); ++m) {
      if (ModuleHasAddable(PageSettingModuleRegistry::Get(m), overrides)) {
        can_add = true;
        break;
      }
    }
    add_button->SetEnabled(can_add);
  }

  if (delete_button != nullptr)
    delete_button->SetEnabled(IsDefined() && GetList().GetLength() > 0);
}

void
PageCustomSettingsWidget::OnAddClicked() noexcept
{
  ComboList group_list;
  StaticArray<PageSettingGroup, unsigned(PageSettingGroup::COUNT)> groups;
  for (unsigned m = 0; m < PageSettingModuleRegistry::Count(); ++m) {
    const auto &module = PageSettingModuleRegistry::Get(m);
    if (!ModuleHasAddable(module, overrides))
      continue;
    group_list.Append(int(module.group), gettext(module.label));
    groups.append(module.group);
  }

  if (group_list.empty())
    return;

  PageSettingGroup setting_group;
  if (groups.size() == 1) {
    setting_group = groups[0];
  } else {
    const int group_result = ComboPicker(_("Add"), group_list, nullptr);
    if (group_result < 0 || unsigned(group_result) >= groups.size())
      return;
    setting_group = groups[group_result];
  }

  const auto &module = PageSettingModuleRegistry::Get(setting_group);

  static constexpr int SECTION_HEADER = -1;

  struct AddableItem {
    PageSettingId id;
    const char *section;
    const char *label;
  };

  StaticArray<AddableItem, unsigned(PageSettingId::COUNT)> items;
  for (unsigned i = 0; i < module.count(); ++i) {
    const auto &desc = module.get(PageSettingId(unsigned(module.id_start) +
                                                i));
    if (AirspaceDisplaySetting::IsClassBorderColor(desc.id) ||
        AirspaceDisplaySetting::IsClassBorderWidth(desc.id) ||
        AirspaceDisplaySetting::IsClassFillMode(desc.id))
      continue;

    if (AirspaceDisplaySetting::IsClassFillColor(desc.id)) {
      const AirspaceClass cls =
        AirspaceDisplaySetting::ClassFromFillColorId(desc.id);
      if (AirspaceDisplaySetting::HasColorOverride(overrides, cls))
        continue;

      StaticString<64> label;
      label.Format(_("%s colours"),
                   gettext(AirspaceFormatter::GetClass(cls)));
      const unsigned label_index = unsigned(cls) - 1;
      StringFormat(color_picker_labels[label_index],
                   sizeof(color_picker_labels[label_index]),
                   "%s", label.c_str());

      auto &item = items.append();
      item.id = desc.id;
      item.section = desc.section;
      item.label = color_picker_labels[label_index];
      continue;
    }

    if (overrides.Contains(desc.id))
      continue;

    auto &item = items.append();
    item.id = desc.id;
    item.section = desc.section;
    item.label = gettext(desc.label);
  }

  if (items.empty())
    return;

  std::sort(items.begin(), items.end(),
            [](const AddableItem &a, const AddableItem &b) noexcept {
              return PageSettingFilterCatalog::CompareSectionAndLabel(
                a.section, a.label, b.section, b.label) < 0;
            });

  ComboList list;
  StaticArray<PageSettingId, unsigned(PageSettingId::COUNT)> ids;
  const char *prev_section = nullptr;
  for (const auto &item : items) {
    if (!SameSection(item.section, prev_section)) {
      if (item.section != nullptr) {
        StaticString<96> header;
        header.Format("---------------- %s ----------------",
                      gettext(item.section));
        list.Append(SECTION_HEADER, header.c_str());
      }
      prev_section = item.section;
    }

    list.Append(ids.size(), item.label);
    ids.append(item.id);
  }

  if (list.empty())
    return;

  StaticString<64> caption;
  caption.Format("%s: %s", _("Add"), gettext(module.label));

  const int result = ComboPicker(caption, list, nullptr);
  if (result < 0 || unsigned(result) >= list.size())
    return;

  const int choice = list[result].int_value;
  if (choice < 0 || unsigned(choice) >= ids.size())
    return;

  const auto id = ids[choice];
  if (AirspaceDisplaySetting::IsClassColor(id))
    AirspaceDisplaySetting::AddColorOverrides(
      overrides, AirspaceDisplaySetting::ClassFromColorId(id));
  else
    overrides.Add(id, PageSettingGet(id));

  RebuildRows();
}

AirspaceRendererSettings
PageCustomSettingsWidget::BuildPreviewRenderer() const noexcept
{
  AirspaceRendererSettings renderer =
    CommonInterface::GetMapSettings().airspace;

  for (unsigned i = 0; i < overrides.n_items; ++i) {
    const PageSettingId id = overrides.items[i].id;
    const int value = overrides.items[i].value;
    if (value == PageSettingOverrides::INHERIT)
      continue;

    if (AirspaceDisplaySetting::IsClassFillColor(id)) {
      const AirspaceClass cls =
        AirspaceDisplaySetting::ClassFromFillColorId(id);
      renderer.classes[unsigned(cls)].fill_color =
        AirspaceClassColorProfile::Unpack(value);
    } else if (AirspaceDisplaySetting::IsClassBorderColor(id)) {
      const AirspaceClass cls =
        AirspaceDisplaySetting::ClassFromBorderColorId(id);
      renderer.classes[unsigned(cls)].border_color =
        AirspaceClassColorProfile::Unpack(value);
    } else if (AirspaceDisplaySetting::IsClassBorderWidth(id)) {
      const AirspaceClass cls =
        AirspaceDisplaySetting::ClassFromBorderWidthId(id);
      renderer.classes[unsigned(cls)].border_width = unsigned(value);
    } else if (AirspaceDisplaySetting::IsClassFillMode(id)) {
      const AirspaceClass cls =
        AirspaceDisplaySetting::ClassFromFillModeId(id);
      renderer.classes[unsigned(cls)].fill_mode =
        AirspaceClassRendererSettings::FillMode(value);
    }
  }

  return renderer;
}

void
PageCustomSettingsWidget::SyncAirspacePreviewLook() const noexcept
{
  if (CommonInterface::main_window == nullptr)
    return;

  CommonInterface::main_window->SetLook().map.airspace.Reinitialise(
    BuildPreviewRenderer());
}

const char *
PageCustomSettingsWidget::FormatValue(PageSettingId id,
                                      int value) const noexcept
{
  const auto &desc = PageSettingRegistry::Get(id);
  DataFieldEnum df(nullptr);
  PageSettingCatalog::FillDataFieldEnum(df, desc, value);
  StringFormat(value_display_buffer, sizeof(value_display_buffer),
               "%s", df.GetAsDisplayString());
  return value_display_buffer;
}

void
PageCustomSettingsWidget::RebuildRows() noexcept
{
  row_ids.clear();

  bool color_class_shown[AIRSPACECLASSCOUNT]{};

  for (unsigned i = 0; i < overrides.n_items; ++i) {
    const PageSettingId id = overrides.items[i].id;
    if (IsColorRow(id)) {
      const AirspaceClass cls = AirspaceDisplaySetting::ClassFromColorId(id);
      if (color_class_shown[unsigned(cls)])
        continue;

      color_class_shown[unsigned(cls)] = true;
      row_ids.append(AirspaceDisplaySetting::FillColorId(cls));
    } else {
      row_ids.append(id);
    }
  }

  if (IsDefined()) {
    GetList().SetLength(row_ids.size());
    GetList().Invalidate();
  }
  SyncAirspacePreviewLook();
  UpdateActionButtons();
}

void
PageCustomSettingsWidget::OnDeleteClicked() noexcept
{
  if (!IsDefined() || GetList().GetLength() == 0)
    return;

  const unsigned index = GetList().GetCursorIndex();
  if (index >= row_ids.size())
    return;

  const auto id = row_ids[index];
  if (IsColorRow(id))
    AirspaceDisplaySetting::RemoveColorOverrides(
      overrides, AirspaceDisplaySetting::ClassFromColorId(id));
  else if (!overrides.Contains(id))
    return;
  else
    overrides.Remove(id);

  RebuildRows();
}

void
PageCustomSettingsWidget::EditColorRow(unsigned index) noexcept
{
  assert(index < row_ids.size());
  assert(IsColorRow(row_ids[index]));

  const AirspaceClass cls =
    AirspaceDisplaySetting::ClassFromColorId(row_ids[index]);

  /* Ensure border width / fill mode slots exist for older colour-only
     overrides so the shared class renderer dialog can edit them. */
  AirspaceDisplaySetting::AddColorOverrides(overrides, cls);

  PageAirspaceRendererSettingsContext page_context;
  page_context.overrides = &overrides;

  if (!ShowAirspaceClassRendererSettingsDialog(cls, page_context))
    return;

  /* Same refresh as dlgAirspace colour mode: only AirspaceLook, then
     invalidate the list.  Do not call MainWindow::ReinitialiseLook(). */
  SyncAirspacePreviewLook();
  GetList().Invalidate();
}

void
PageCustomSettingsWidget::EditValueRow(unsigned index) noexcept
{
  assert(index < row_ids.size());
  assert(!IsColorRow(row_ids[index]));

  const PageSettingId id = row_ids[index];
  const auto &desc = PageSettingRegistry::Get(id);
  const int *v = overrides.FindValue(id);
  assert(v != nullptr);

  DataFieldEnum df(nullptr);
  PageSettingCatalog::FillDataFieldEnum(df, desc, *v);
  df.EnableItemHelp(true);

  if (!ComboPicker(PageSettingCatalog::GettextOptional(desc.label), df,
                   PageSettingCatalog::GettextOptional(desc.help_global)))
    return;

  overrides.SetValue(id, int(df.GetValue()));
  GetList().Invalidate();
}

void
PageCustomSettingsWidget::Prepare(ContainerWindow &parent,
                                  const PixelRect &rc) noexcept
{
  const DialogLook &look = UIGlobals::GetDialogLook();
  ListControl &list = CreateList(parent, look, rc,
                                 row_renderer.CalculateLayout(*look.list.font));
  RebuildRows();
  list.SetLength(row_ids.size());
  UpdateActionButtons();
}

void
PageCustomSettingsWidget::OnPaintItem(Canvas &canvas, const PixelRect rc,
                                      unsigned idx) noexcept
{
  assert(idx < row_ids.size());

  const PageSettingId id = row_ids[idx];
  PixelRect remaining = rc;

  if (IsColorRow(id)) {
    const AirspaceClass cls = AirspaceDisplaySetting::ClassFromColorId(id);
    StaticString<64> label;
    label.Format(_("%s colours"), gettext(AirspaceFormatter::GetClass(cls)));

    const int second_x = row_renderer.NextColumn(canvas, remaining, label);

    if (CommonInterface::main_window != nullptr) {
      /* Same sources as dlgAirspace colour list: live look (synced to
         page overrides via SyncAirspacePreviewLook) + map settings. */
      const AirspaceRendererSettings &renderer =
        CommonInterface::GetMapSettings().airspace;
      const AirspaceLook &airspace_look =
        CommonInterface::main_window->GetLook().map.airspace;
      const int padding = Layout::GetTextPadding();
      AirspacePreviewRenderer::DrawClassSwatch(
        canvas,
        {second_x, remaining.top + padding, remaining.right - padding,
         remaining.bottom - padding},
        cls, airspace_look, renderer);
    }

    row_renderer.DrawTextRow(canvas, remaining, label);
    return;
  }

  const auto &desc = PageSettingRegistry::Get(id);
  const int *v = overrides.FindValue(id);
  assert(v != nullptr);

  remaining.right = row_renderer.DrawRightColumn(canvas, remaining,
                                                 FormatValue(id, *v));
  row_renderer.DrawTextRow(canvas, remaining,
                           PageSettingCatalog::GettextOptional(desc.label));
}

void
PageCustomSettingsWidget::OnCursorMoved(unsigned) noexcept
{
  UpdateActionButtons();
}

bool
PageCustomSettingsWidget::CanActivateItem(unsigned) const noexcept
{
  return true;
}

void
PageCustomSettingsWidget::OnActivateItem(unsigned index) noexcept
{
  assert(index < row_ids.size());

  if (IsColorRow(row_ids[index]))
    EditColorRow(index);
  else
    EditValueRow(index);
}

void
ShowPageCustomSettingsDialog(PageSettingOverrides &overrides) noexcept
{
  const DialogLook &look = UIGlobals::GetDialogLook();
  WidgetDialog dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
                      look, _("Custom settings"));

  auto list = std::make_unique<PageCustomSettingsWidget>(overrides);
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

  /* Restore map airspace look from live settings — preview sync above
     only applied this page's draft overrides for the list swatches. */
  if (CommonInterface::main_window != nullptr)
    CommonInterface::main_window->SetLook().map.airspace.Reinitialise(
      CommonInterface::GetMapSettings().airspace);
}
