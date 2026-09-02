// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WaypointDisplayConfigPanel.hpp"
#include "Waypoints/WaypointsDisplaySetting.hpp"
#include "Dialogs/Settings/DisplaySettingConfigPanel.hpp"
#include "ActionInterface.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Listener.hpp"
#include "Interface.hpp"
#include "PageSetting.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"

#include <cassert>

namespace {

enum ControlIndex : unsigned {
  LABEL_FORMAT = 0,
  ARRIVAL_HEIGHT,
  LABEL_STYLE,
  LABEL_VISIBILITY,
  LANDABLE_SYMBOLS,
  ICON_SCALE,
  DETAILED_LANDABLES,
  LANDABLE_SIZE,
  SCALE_RUNWAY_LENGTH,

  COUNT
};

static_assert(unsigned(ControlIndex::COUNT) == PageSettingWaypointsCount,
              "Waypoints config controls must match catalog size");

[[nodiscard]]
bool
IsExpertRow(unsigned control) noexcept
{
  switch (ControlIndex(control)) {
  case ControlIndex::ARRIVAL_HEIGHT:
  case ControlIndex::LABEL_STYLE:
  case ControlIndex::LABEL_VISIBILITY:
  case ControlIndex::DETAILED_LANDABLES:
  case ControlIndex::LANDABLE_SIZE:
  case ControlIndex::SCALE_RUNWAY_LENGTH:
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

private:
  /* methods from DataFieldListener */
  void OnModified(DataField &df) noexcept override;
};

void
WaypointDisplayConfigPanel::SyncBundleFromForm() noexcept
{
  DisplaySettingConfigPanel::SyncBundleFromForm(
    *this, bundle, PageSettingWaypointsStart, PageSettingWaypointsCount,
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
  const bool show = bundle.waypoint.vector_landable_rendering;
  SetRowVisible(unsigned(ControlIndex::LANDABLE_SIZE), show);
  SetRowVisible(unsigned(ControlIndex::SCALE_RUNWAY_LENGTH), show);
}

void
WaypointDisplayConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  if (IsDataField(unsigned(ControlIndex::DETAILED_LANDABLES), df))
    UpdateVisibilities();

  ApplyBundleLive();
}

void
WaypointDisplayConfigPanel::Prepare(ContainerWindow &parent,
                                    const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  WaypointsDisplaySetting::ReadLive(bundle);

  DisplaySettingConfigPanel::AddCatalogRows(
    *this, bundle, PageSettingWaypointsStart, PageSettingWaypointsCount,
    WaypointsDisplaySetting::Get, WaypointsDisplaySetting::GetValue,
    IsExpertRow, this, nullptr,
    unsigned(ControlIndex::DETAILED_LANDABLES));

  SyncBundleFromForm();
  initial_bundle = bundle;
  UpdateVisibilities();
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
