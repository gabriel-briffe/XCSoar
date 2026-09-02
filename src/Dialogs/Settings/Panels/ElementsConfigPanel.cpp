// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "ElementsConfigPanel.hpp"
#include "Elements/ElementsDisplaySetting.hpp"
#include "Dialogs/Settings/DisplaySettingConfigPanel.hpp"
#include "ActionInterface.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Listener.hpp"
#include "Interface.hpp"
#include "PageSetting.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"

namespace {

[[nodiscard]]
bool
IsExpertRow(unsigned control) noexcept
{
  switch (PageSettingId(PageSettingElementsStart + control)) {
  case PageSettingId::TRAIL_LENGTH:
  case PageSettingId::TRAIL_DRIFT:
  case PageSettingId::TRAIL_TYPE:
  case PageSettingId::TRAIL_SCALED:
  case PageSettingId::DETOUR_COST_MARKERS:
  case PageSettingId::AIRCRAFT_SYMBOL:
  case PageSettingId::WIND_ARROW_STYLE:
    return true;
  default:
    return false;
  }
}

} // namespace

class ElementsConfigPanel final
  : public RowFormWidget, DataFieldListener {
  ElementsDisplaySetting::Bundle bundle;
  ElementsDisplaySetting::Bundle initial_bundle;

  void SyncBundleFromForm() noexcept;
  void ApplyBundleLive() noexcept;

public:
  ElementsConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void ShowTrailControls(bool show);

  /* methods from Widget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;

private:
  /* methods from DataFieldListener */
  void OnModified(DataField &df) noexcept override;
};

void
ElementsConfigPanel::SyncBundleFromForm() noexcept
{
  DisplaySettingConfigPanel::SyncBundleFromForm(
    *this, bundle, PageSettingElementsStart, PageSettingElementsCount,
    ElementsDisplaySetting::Get, ElementsDisplaySetting::SetValue);
}

void
ElementsConfigPanel::ApplyBundleLive() noexcept
{
  const TrailSettings old_trail = CommonInterface::GetMapSettings().trail;
  ElementsDisplaySetting::ApplyLive(bundle);
  PageSettingReinitialiseTrailLookIfChanged(old_trail);
  ActionInterface::SendMapSettings(true);
}

void
ElementsConfigPanel::ShowTrailControls(bool show)
{
  using DisplaySettingConfigPanel::CatalogRow;

  SetRowVisible(CatalogRow(PageSettingId::TRAIL_DRIFT,
                           PageSettingElementsStart), show);
  SetRowVisible(CatalogRow(PageSettingId::TRAIL_TYPE,
                           PageSettingElementsStart), show);
  SetRowVisible(CatalogRow(PageSettingId::TRAIL_SCALED,
                           PageSettingElementsStart), show);
}

void
ElementsConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  using DisplaySettingConfigPanel::CatalogRow;

  if (IsDataField(CatalogRow(PageSettingId::TRAIL_LENGTH,
                             PageSettingElementsStart), df))
    ShowTrailControls(bundle.trail.length != TrailSettings::Length::OFF);

  ApplyBundleLive();
}

void
ElementsConfigPanel::Prepare(ContainerWindow &parent,
                             const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  ElementsDisplaySetting::ReadLive(bundle);

  using DisplaySettingConfigPanel::CatalogRow;

  DisplaySettingConfigPanel::AddCatalogRows(
    *this, bundle, PageSettingElementsStart, PageSettingElementsCount,
    ElementsDisplaySetting::Get, ElementsDisplaySetting::GetValue,
    IsExpertRow, this, nullptr,
    CatalogRow(PageSettingId::TRAIL_LENGTH, PageSettingElementsStart));

  SyncBundleFromForm();
  initial_bundle = bundle;
  ShowTrailControls(bundle.trail.length != TrailSettings::Length::OFF);
}

bool
ElementsConfigPanel::Save(bool &_changed) noexcept
{
  SyncBundleFromForm();
  ElementsDisplaySetting::ApplyLive(bundle);
  _changed |= ElementsDisplaySetting::SaveGlobal(bundle, initial_bundle);
  return true;
}

std::unique_ptr<Widget>
CreateElementsConfigPanel()
{
  return std::make_unique<ElementsConfigPanel>();
}
