// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WaypointDisplayConfigPanel.hpp"
#include "Waypoints/WaypointsDisplaySetting.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/Settings/DisplaySettingConfigPanel.hpp"
#include "Dialogs/Waypoint/WaypointDialogs.hpp"
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
  switch (PageSettingId(PageSettingWaypointsStart + control)) {
  case PageSettingId::WAYPOINT_ARRIVAL_HEIGHT:
  case PageSettingId::WAYPOINT_LABEL_STYLE:
  case PageSettingId::WAYPOINT_LABEL_VISIBILITY:
  case PageSettingId::WAYPOINT_DETAILED_LANDABLES:
  case PageSettingId::WAYPOINT_LANDABLE_SIZE:
  case PageSettingId::WAYPOINT_SCALE_RUNWAY_LENGTH:
    return true;
  default:
    return false;
  }
}

} // namespace

class WaypointDisplayConfigPanel final
  : public RowFormWidget, DataFieldListener {
  WaypointsDisplaySetting::Bundle bundle;
  WaypointsDisplaySetting::Bundle initial_bundle;

  void SyncBundleFromForm() noexcept;
  void ApplyBundleLive() noexcept;

public:
  WaypointDisplayConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void UpdateVisibilities();

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
WaypointDisplayConfigPanel::SyncBundleFromForm() noexcept
{
  DisplaySettingConfigPanel::SyncBundleFromForm(
    *this, bundle, PageSettingWaypointsStart, PageSettingWaypointsBaseCount,
    WaypointsDisplaySetting::Get, WaypointsDisplaySetting::SetValue);
}

void
WaypointDisplayConfigPanel::ApplyBundleLive() noexcept
{
  const WaypointRendererSettings old_waypoint =
    CommonInterface::GetMapSettings().waypoint;

  WaypointsDisplaySetting::ApplyLive(bundle);
  PageSettingReinitialiseWaypointLookIfChanged(old_waypoint);
  ActionInterface::SendMapSettings(true);
}

void
WaypointDisplayConfigPanel::UpdateVisibilities()
{
  using DisplaySettingConfigPanel::CatalogRow;

  const bool show = bundle.waypoint.vector_landable_rendering;
  SetRowVisible(CatalogRow(PageSettingId::WAYPOINT_LANDABLE_SIZE,
                           PageSettingWaypointsStart), show);
  SetRowVisible(CatalogRow(PageSettingId::WAYPOINT_SCALE_RUNWAY_LENGTH,
                           PageSettingWaypointsStart), show);
}

void
WaypointDisplayConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  using DisplaySettingConfigPanel::CatalogRow;

  if (IsDataField(CatalogRow(PageSettingId::WAYPOINT_DETAILED_LANDABLES,
                             PageSettingWaypointsStart), df))
    UpdateVisibilities();

  ApplyBundleLive();
}

void
WaypointDisplayConfigPanel::Prepare(ContainerWindow &parent,
                                    const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  WaypointsDisplaySetting::ReadLive(bundle);

  using DisplaySettingConfigPanel::CatalogRow;

  DisplaySettingConfigPanel::AddCatalogRows(
    *this, bundle, PageSettingWaypointsStart, PageSettingWaypointsBaseCount,
    WaypointsDisplaySetting::Get, WaypointsDisplaySetting::GetValue,
    IsExpertRow, this, nullptr,
    CatalogRow(PageSettingId::WAYPOINT_DETAILED_LANDABLES,
               PageSettingWaypointsStart));

  SyncBundleFromForm();
  initial_bundle = bundle;
  UpdateVisibilities();
}

void
WaypointDisplayConfigPanel::Show(const PixelRect &rc) noexcept
{
  ConfigPanel::BorrowExtraButton(2, _("Filter"), [](){
    dlgWaypointFilterShowModal();
  });

  RowFormWidget::Show(rc);
}

void
WaypointDisplayConfigPanel::Hide() noexcept
{
  RowFormWidget::Hide();
  ConfigPanel::ReturnExtraButton(2);
}

bool
WaypointDisplayConfigPanel::Save(bool &_changed) noexcept
{
  SyncBundleFromForm();
  WaypointsDisplaySetting::ApplyLive(bundle);
  _changed |= WaypointsDisplaySetting::SaveGlobal(bundle, initial_bundle);
  return true;
}

std::unique_ptr<Widget>
CreateWaypointDisplayConfigPanel()
{
  return std::make_unique<WaypointDisplayConfigPanel>();
}
