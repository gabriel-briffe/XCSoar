// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AirspaceConfigPanel.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "ConfigPanel.hpp"
#include "DisplaySettingConfigPanel.hpp"
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
PageSettingId
CatalogIdForControl(unsigned control) noexcept
{
  assert(control < PageSettingAirspaceBaseCount);
  return PageSettingId(PageSettingAirspaceStart + control);
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
  for (unsigned control = 0; control < PageSettingAirspaceBaseCount; ++control) {
    const auto id = CatalogIdForControl(control);
    const auto &desc = AirspaceDisplaySetting::Get(id);

    switch (desc.type) {
    case PageSettingType::BOOL:
      AirspaceDisplaySetting::SetValue(bundle, id,
                                       GetValueBoolean(control) ? 1 : 0);
      break;

    case PageSettingType::ENUM:
      AirspaceDisplaySetting::SetValue(bundle, id,
                                       int(GetValueEnum(control)));
      break;

    case PageSettingType::INTEGER:
      AirspaceDisplaySetting::SetValue(bundle, id,
                                       GetValueInteger(control));
      break;
    }
  }
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

  auto AddCatalogRow = [this](PageSettingId id,
                              DataFieldListener *listener = nullptr) {
    const auto &desc = AirspaceDisplaySetting::Get(id);
    DisplaySettingConfigPanel::AddRow(
      *this, desc, AirspaceDisplaySetting::GetValue(bundle, id), listener);
  };

  AddCatalogRow(PageSettingId::AIRSPACE_DISPLAY, this);
  AddCatalogRow(PageSettingId::AIRSPACE_LABEL_VISIBILITY);
  SetExpertRow(unsigned(ControlIndex::LABEL_VISIBILITY));

  AddCatalogRow(PageSettingId::AIRSPACE_SHOW_NOTAM_LABELS);
  SetExpertRow(unsigned(ControlIndex::SHOW_NOTAM_LABELS));

  AddCatalogRow(PageSettingId::AIRSPACE_CLIP_ALTITUDE);
  AddCatalogRow(PageSettingId::AIRSPACE_MARGIN);

  AddCatalogRow(PageSettingId::AIRSPACE_WARNINGS, this);

  AddCatalogRow(PageSettingId::AIRSPACE_WARNING_DIALOG);
  SetExpertRow(unsigned(ControlIndex::WARNING_DIALOG));

  AddCatalogRow(PageSettingId::AIRSPACE_WARNING_TIME);
  SetExpertRow(unsigned(ControlIndex::WARNING_TIME));

  AddCatalogRow(PageSettingId::AIRSPACE_REPETITIVE_SOUND);
  SetExpertRow(unsigned(ControlIndex::REPETITIVE_SOUND));

  AddCatalogRow(PageSettingId::AIRSPACE_ACKNOWLEDGE_TIME);
  SetExpertRow(unsigned(ControlIndex::ACKNOWLEDGE_TIME));

  AddCatalogRow(PageSettingId::AIRSPACE_BLACK_OUTLINE);
  SetExpertRow(unsigned(ControlIndex::BLACK_OUTLINE));

  AddCatalogRow(PageSettingId::AIRSPACE_FILL_MODE);
  SetExpertRow(unsigned(ControlIndex::FILL_MODE));

  AddCatalogRow(PageSettingId::AIRSPACE_TRANSPARENCY);
  SetExpertRow(unsigned(ControlIndex::TRANSPARENCY));

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
