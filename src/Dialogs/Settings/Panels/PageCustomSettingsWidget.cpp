// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageCustomSettingsWidget.hpp"

#include "Dialogs/Airspace/Airspace.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Dialogs/WidgetDialog.hpp"
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
#include "util/TruncateString.hpp"

#include <algorithm>
#include <cassert>
#include <memory>

class PageCustomSettingsWidget final : public ListWidget {
  PageSettingOverrides &overrides;
  Button *add_button = nullptr;
  Button *delete_button = nullptr;
  TextRowRenderer row_renderer;
  /** True after preview Look was applied from colour overrides. */
  mutable bool airspace_preview_active = false;

  struct Row {
    enum class Kind : uint8_t {
      SETTING,
      GROUP_HEADER,
    } kind;

    PageSettingId id;
    PageSettingGroup group;
  };

  static constexpr unsigned MAX_ROWS =
    PageSettingOverrides::MAX_ITEMS + unsigned(PageSettingGroup::COUNT);

  StaticArray<Row, MAX_ROWS> rows;

  [[nodiscard]]
  static bool
  IsColorRow(PageSettingId id) noexcept
  {
    return AirspaceDisplaySetting::IsClassColor(id);
  }

  [[nodiscard]]
  bool
  IsHeaderRow(unsigned index) const noexcept
  {
    return index < rows.size() &&
           rows[index].kind == Row::Kind::GROUP_HEADER;
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

  /**
   * Sync preview Look when colour overrides exist; restore live Look when
   * the last colour override was removed (skip when never previewing).
   */
  void SyncOrRestoreAirspacePreviewLook() const noexcept;

  [[nodiscard]]
  static bool
  OverridesHaveColor(const PageSettingOverrides &overrides) noexcept;

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
IsAddableCatalogEntry(const PageSettingDescriptor &desc,
                      const PageSettingOverrides &overrides) noexcept
{
  if (AirspaceDisplaySetting::IsClassBorderColor(desc.id) ||
      AirspaceDisplaySetting::IsClassBorderWidth(desc.id) ||
      AirspaceDisplaySetting::IsClassFillMode(desc.id))
    return false;

  if (AirspaceDisplaySetting::IsClassFillColor(desc.id))
    return !AirspaceDisplaySetting::HasColorOverride(
      overrides, AirspaceDisplaySetting::ClassFromFillColorId(desc.id));

  return !overrides.Contains(desc.id);
}

[[nodiscard]]
bool
ModuleHasAddable(const PageSettingModule &module,
                 const PageSettingOverrides &overrides) noexcept
{
  for (unsigned i = 0; i < module.count(); ++i) {
    const auto &desc = module.get(PageSettingId(unsigned(module.id_start) +
                                                i));
    if (IsAddableCatalogEntry(desc, overrides))
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

void
FormatGroupHeader(char *buffer, size_t size, const char *label) noexcept
{
  StringFormat(buffer, size, "---------------- %s ----------------",
               gettext(label));
}

void
FormatClassColoursLabel(char *buffer, size_t size,
                        AirspaceClass cls) noexcept
{
  StringFormat(buffer, size, _("%s colours"),
               gettext(AirspaceFormatter::GetClass(cls)));
}

static char color_picker_labels[PageSettingAirspaceClassFillColorCount][64];
static char value_display_buffer[64];
static char header_display_buffer[96];

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

  if (delete_button != nullptr) {
    const bool has_rows = IsDefined() && GetList().GetLength() > 0;
    const unsigned cursor =
      has_rows ? GetList().GetCursorIndex() : 0;
    delete_button->SetEnabled(has_rows && !IsHeaderRow(cursor));
  }
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
    if (!IsAddableCatalogEntry(desc, overrides))
      continue;

    if (AirspaceDisplaySetting::IsClassFillColor(desc.id)) {
      const AirspaceClass cls =
        AirspaceDisplaySetting::ClassFromFillColorId(desc.id);
      const unsigned label_index = unsigned(cls) - 1;
      FormatClassColoursLabel(color_picker_labels[label_index],
                              sizeof(color_picker_labels[label_index]),
                              cls);

      auto &item = items.append();
      item.id = desc.id;
      item.section = desc.section;
      item.label = color_picker_labels[label_index];
      continue;
    }

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
        FormatGroupHeader(header_display_buffer,
                          sizeof(header_display_buffer), item.section);
        list.Append(SECTION_HEADER, header_display_buffer);
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
  AirspaceDisplaySetting::Bundle bundle{};
  bundle.airspace = CommonInterface::GetMapSettings().airspace;

  for (unsigned i = 0; i < overrides.n_items; ++i) {
    const PageSettingId id = overrides.items[i].id;
    const int value = overrides.items[i].value;
    if (value == PageSettingOverrides::INHERIT)
      continue;
    if (!AirspaceDisplaySetting::IsClassColor(id))
      continue;

    AirspaceDisplaySetting::SetValue(bundle, id, value);
  }

  return bundle.airspace;
}

void
PageCustomSettingsWidget::SyncAirspacePreviewLook() const noexcept
{
  if (CommonInterface::main_window == nullptr)
    return;

  CommonInterface::main_window->SetLook().map.airspace.Reinitialise(
    BuildPreviewRenderer());
  airspace_preview_active = true;
}

bool
PageCustomSettingsWidget::OverridesHaveColor(
  const PageSettingOverrides &overrides) noexcept
{
  for (unsigned i = 0; i < overrides.n_items; ++i)
    if (AirspaceDisplaySetting::IsClassColor(overrides.items[i].id))
      return true;
  return false;
}

void
PageCustomSettingsWidget::SyncOrRestoreAirspacePreviewLook() const noexcept
{
  if (CommonInterface::main_window == nullptr)
    return;

  if (OverridesHaveColor(overrides)) {
    SyncAirspacePreviewLook();
    return;
  }

  if (!airspace_preview_active)
    return;

  CommonInterface::main_window->SetLook().map.airspace.Reinitialise(
    CommonInterface::GetMapSettings().airspace);
  airspace_preview_active = false;
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
  rows.clear();

  bool color_class_shown[AIRSPACECLASSCOUNT]{};
  StaticArray<PageSettingId, PageSettingOverrides::MAX_ITEMS> setting_ids;

  for (unsigned i = 0; i < overrides.n_items; ++i) {
    const PageSettingId id = overrides.items[i].id;
    if (IsColorRow(id)) {
      const AirspaceClass cls = AirspaceDisplaySetting::ClassFromColorId(id);
      if (color_class_shown[unsigned(cls)])
        continue;

      color_class_shown[unsigned(cls)] = true;
      setting_ids.append(AirspaceDisplaySetting::FillColorId(cls));
    } else {
      setting_ids.append(id);
    }
  }

  std::sort(setting_ids.begin(), setting_ids.end(),
            [](PageSettingId a, PageSettingId b) noexcept {
              const PageSettingGroup group_a = PageSettingGroupForId(a);
              const PageSettingGroup group_b = PageSettingGroupForId(b);
              if (group_a != group_b)
                return unsigned(group_a) < unsigned(group_b);

              char label_a[96];
              char label_b[96];
              if (AirspaceDisplaySetting::IsClassColor(a)) {
                const AirspaceClass cls =
                  AirspaceDisplaySetting::ClassFromColorId(a);
                FormatClassColoursLabel(label_a, sizeof(label_a), cls);
              } else {
                CopyTruncateString(label_a, sizeof(label_a),
                                   PageSettingCatalog::GettextOptional(
                                     PageSettingRegistry::Get(a).label));
              }

              if (AirspaceDisplaySetting::IsClassColor(b)) {
                const AirspaceClass cls =
                  AirspaceDisplaySetting::ClassFromColorId(b);
                FormatClassColoursLabel(label_b, sizeof(label_b), cls);
              } else {
                CopyTruncateString(label_b, sizeof(label_b),
                                   PageSettingCatalog::GettextOptional(
                                     PageSettingRegistry::Get(b).label));
              }

              return StringCompareIgnoreCase(label_a, label_b) < 0;
            });

  PageSettingGroup prev_group = PageSettingGroup::COUNT;
  for (const PageSettingId id : setting_ids) {
    const PageSettingGroup group = PageSettingGroupForId(id);
    if (group != prev_group) {
      auto &header = rows.append();
      header.kind = Row::Kind::GROUP_HEADER;
      header.id = PageSettingId::COUNT;
      header.group = group;
      prev_group = group;
    }

    auto &row = rows.append();
    row.kind = Row::Kind::SETTING;
    row.id = id;
    row.group = group;
  }

  if (IsDefined()) {
    GetList().SetLength(rows.size());
    GetList().Invalidate();
  }
  SyncOrRestoreAirspacePreviewLook();
  UpdateActionButtons();
}

void
PageCustomSettingsWidget::OnDeleteClicked() noexcept
{
  if (!IsDefined() || GetList().GetLength() == 0)
    return;

  const unsigned index = GetList().GetCursorIndex();
  if (index >= rows.size() || IsHeaderRow(index))
    return;

  const auto id = rows[index].id;
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
  assert(index < rows.size());
  assert(rows[index].kind == Row::Kind::SETTING);
  assert(IsColorRow(rows[index].id));

  const AirspaceClass cls =
    AirspaceDisplaySetting::ClassFromColorId(rows[index].id);

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
  assert(index < rows.size());
  assert(rows[index].kind == Row::Kind::SETTING);
  assert(!IsColorRow(rows[index].id));

  const PageSettingId id = rows[index].id;
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
  list.SetLength(rows.size());
  UpdateActionButtons();
}

void
PageCustomSettingsWidget::OnPaintItem(Canvas &canvas, const PixelRect rc,
                                      unsigned idx) noexcept
{
  assert(idx < rows.size());

  const Row &row = rows[idx];
  if (row.kind == Row::Kind::GROUP_HEADER) {
    FormatGroupHeader(header_display_buffer, sizeof(header_display_buffer),
                      PageSettingModuleRegistry::GetLabel(row.group));
    row_renderer.DrawTextRow(canvas, rc, header_display_buffer);
    return;
  }

  const PageSettingId id = row.id;
  PixelRect remaining = rc;

  if (IsColorRow(id)) {
    const AirspaceClass cls = AirspaceDisplaySetting::ClassFromColorId(id);
    char label[64];
    FormatClassColoursLabel(label, sizeof(label), cls);

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
PageCustomSettingsWidget::CanActivateItem(unsigned index) const noexcept
{
  return !IsHeaderRow(index);
}

void
PageCustomSettingsWidget::OnActivateItem(unsigned index) noexcept
{
  assert(index < rows.size());
  assert(!IsHeaderRow(index));

  if (IsColorRow(rows[index].id))
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
