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

#include <cassert>

namespace {

enum ControlIndex : unsigned {
  DISPLAY = 0,
  LABEL_VISIBILITY,
  SHOW_NOTAM_LABELS,
  CLIP_ALTITUDE,
  MARGIN,
  WARNINGS,
  WARNING_DIALOG,
  WARNING_TIME,
  REPETITIVE_SOUND,
  ACKNOWLEDGE_TIME,
  BLACK_OUTLINE,
  FILL_MODE,
  TRANSPARENCY,

  COUNT
};

static_assert(unsigned(ControlIndex::COUNT) == PageSettingAirspaceBaseCount,
              "Airspace config controls must match base catalog size");

[[nodiscard]]
bool
IsExpertRow(unsigned control) noexcept
{
  switch (ControlIndex(control)) {
  case ControlIndex::LABEL_VISIBILITY:
  case ControlIndex::SHOW_NOTAM_LABELS:
  case ControlIndex::WARNING_DIALOG:
  case ControlIndex::WARNING_TIME:
  case ControlIndex::REPETITIVE_SOUND:
  case ControlIndex::ACKNOWLEDGE_TIME:
  case ControlIndex::BLACK_OUTLINE:
  case ControlIndex::FILL_MODE:
  case ControlIndex::TRANSPARENCY:
    return true;
  default:
    return false;
  }
}

[[nodiscard]]
bool
NeedsListener(unsigned control) noexcept
{
  return control == unsigned(ControlIndex::DISPLAY) ||
         control == unsigned(ControlIndex::WARNINGS);
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
  SetRowVisible(unsigned(ControlIndex::CLIP_ALTITUDE),
                mode == AirspaceDisplayMode::CLIP);

  SetRowVisible(unsigned(ControlIndex::MARGIN),
                mode == AirspaceDisplayMode::AUTO ||
                mode == AirspaceDisplayMode::ALLBELOW);
}

void
AirspaceConfigPanel::ShowWarningControls(bool visible)
{
  SetRowVisible(unsigned(ControlIndex::WARNING_DIALOG), visible);
  SetRowVisible(unsigned(ControlIndex::WARNING_TIME), visible);
  SetRowVisible(unsigned(ControlIndex::REPETITIVE_SOUND), visible);
  SetRowVisible(unsigned(ControlIndex::ACKNOWLEDGE_TIME), visible);
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
  if (IsDataField(unsigned(ControlIndex::DISPLAY), df)) {
    const DataFieldEnum &dfe = (const DataFieldEnum &)df;
    AirspaceDisplayMode mode = (AirspaceDisplayMode)dfe.GetValue();
    ShowDisplayControls(mode);
  } else if (IsDataField(unsigned(ControlIndex::WARNINGS), df)) {
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
