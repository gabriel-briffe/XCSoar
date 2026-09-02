// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AirspaceConfigPanel.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/Settings/DisplaySettingConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Boolean.hpp"
#include "Form/DataField/Listener.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Dialogs/Airspace/Airspace.hpp"
#include "Language/Language.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"
#include "Interface.hpp"
#include "PageSetting.hpp"
#include "UIGlobals.hpp"

namespace {

[[nodiscard]]
bool
IsExpertRow(unsigned control) noexcept
{
  switch (PageSettingId(PageSettingAirspaceStart + control)) {
  case PageSettingId::AIRSPACE_LABEL_VISIBILITY:
  case PageSettingId::AIRSPACE_SHOW_NOTAM_LABELS:
  case PageSettingId::AIRSPACE_WARNING_DIALOG:
  case PageSettingId::AIRSPACE_WARNING_TIME:
  case PageSettingId::AIRSPACE_REPETITIVE_SOUND:
  case PageSettingId::AIRSPACE_ACKNOWLEDGE_TIME:
  case PageSettingId::AIRSPACE_BLACK_OUTLINE:
  case PageSettingId::AIRSPACE_FILL_MODE:
  case PageSettingId::AIRSPACE_TRANSPARENCY:
    return true;
  default:
    return false;
  }
}

[[nodiscard]]
bool
NeedsListener(unsigned control) noexcept
{
  using DisplaySettingConfigPanel::CatalogRow;

  return control == CatalogRow(PageSettingId::AIRSPACE_DISPLAY,
                               PageSettingAirspaceStart) ||
         control == CatalogRow(PageSettingId::AIRSPACE_WARNINGS,
                               PageSettingAirspaceStart);
}

} // namespace

class AirspaceConfigPanel final
  : public RowFormWidget, DataFieldListener {
  AirspaceDisplaySetting::Bundle bundle;
  AirspaceDisplaySetting::Bundle initial_bundle;

  void SyncCatalogFromForm() noexcept;

public:
  AirspaceConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void ShowDisplayControls(AirspaceDisplayMode mode);
  void ShowWarningControls(bool visible);

  /* methods from Widget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
  void Show(const PixelRect &rc) noexcept override;
  void Hide() noexcept override;

private:
  /* methods from DataFieldListener */
  void OnModified(DataField &df) noexcept override;
};

void
AirspaceConfigPanel::SyncCatalogFromForm() noexcept
{
  DisplaySettingConfigPanel::SyncBundleFromForm(
    *this, bundle, PageSettingAirspaceStart, PageSettingAirspaceBaseCount,
    AirspaceDisplaySetting::Get, AirspaceDisplaySetting::SetValue);
}

void
AirspaceConfigPanel::ShowDisplayControls(AirspaceDisplayMode mode)
{
  using DisplaySettingConfigPanel::CatalogRow;

  SetRowVisible(CatalogRow(PageSettingId::AIRSPACE_CLIP_ALTITUDE,
                           PageSettingAirspaceStart),
                mode == AirspaceDisplayMode::CLIP);

  SetRowVisible(CatalogRow(PageSettingId::AIRSPACE_MARGIN,
                           PageSettingAirspaceStart),
                mode == AirspaceDisplayMode::AUTO ||
                mode == AirspaceDisplayMode::ALLBELOW);
}

void
AirspaceConfigPanel::ShowWarningControls(bool visible)
{
  using DisplaySettingConfigPanel::CatalogRow;

  SetRowVisible(CatalogRow(PageSettingId::AIRSPACE_WARNING_DIALOG,
                           PageSettingAirspaceStart), visible);
  SetRowVisible(CatalogRow(PageSettingId::AIRSPACE_WARNING_TIME,
                           PageSettingAirspaceStart), visible);
  SetRowVisible(CatalogRow(PageSettingId::AIRSPACE_REPETITIVE_SOUND,
                           PageSettingAirspaceStart), visible);
  SetRowVisible(CatalogRow(PageSettingId::AIRSPACE_ACKNOWLEDGE_TIME,
                           PageSettingAirspaceStart), visible);
}

void
AirspaceConfigPanel::Show(const PixelRect &rc) noexcept
{
  ConfigPanel::BorrowExtraButton(1, _("Colours"), [](){
    dlgAirspaceShowModal(true);
  });

  ConfigPanel::BorrowExtraButton(2, _("Filter"), [](){
    dlgAirspaceShowModal(false);
  });

  RowFormWidget::Show(rc);
}

void
AirspaceConfigPanel::Hide() noexcept
{
  RowFormWidget::Hide();
  ConfigPanel::ReturnExtraButton(1);
  ConfigPanel::ReturnExtraButton(2);
}

void
AirspaceConfigPanel::OnModified(DataField &df) noexcept
{
  using DisplaySettingConfigPanel::CatalogRow;

  if (IsDataField(CatalogRow(PageSettingId::AIRSPACE_DISPLAY,
                             PageSettingAirspaceStart), df)) {
    const DataFieldEnum &dfe = (const DataFieldEnum &)df;
    AirspaceDisplayMode mode = (AirspaceDisplayMode)dfe.GetValue();
    ShowDisplayControls(mode);
  } else if (IsDataField(CatalogRow(PageSettingId::AIRSPACE_WARNINGS,
                                    PageSettingAirspaceStart), df)) {
    const DataFieldBoolean &dfb = (const DataFieldBoolean &)df;
    ShowWarningControls(dfb.GetValue());
  }
}

void
AirspaceConfigPanel::Prepare(ContainerWindow &parent,
                             const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  AirspaceDisplaySetting::ReadLive(bundle);

  DisplaySettingConfigPanel::AddCatalogRows(
    *this, bundle, PageSettingAirspaceStart, PageSettingAirspaceBaseCount,
    AirspaceDisplaySetting::Get, AirspaceDisplaySetting::GetValue,
    IsExpertRow, this, NeedsListener);

  SyncCatalogFromForm();
  initial_bundle = bundle;

  ShowDisplayControls(bundle.airspace.altitude_mode);
  ShowWarningControls(bundle.computer.enable_warnings);
}

bool
AirspaceConfigPanel::Save(bool &_changed) noexcept
{
  SyncCatalogFromForm();
  AirspaceDisplaySetting::ApplyLive(bundle);
  _changed |= AirspaceDisplaySetting::SaveGlobal(bundle, initial_bundle);
  return true;
}

std::unique_ptr<Widget>
CreateAirspaceConfigPanel()
{
  return std::make_unique<AirspaceConfigPanel>();
}
