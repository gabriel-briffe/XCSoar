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

#include <cassert>

namespace {

enum ControlIndex : unsigned {
  GROUND_TRACK = 0,
  FLARM_TRAFFIC,
  FADE_TRAFFIC,
  TRAIL_LENGTH,
  TRAIL_DRIFT,
  TRAIL_TYPE,
  TRAIL_SCALED,
  DETOUR_COST_MARKERS,
  AIRCRAFT_SYMBOL,
  WIND_ARROW_STYLE,
  ONLINE_TRAFFIC_MAP_MODE,
  DISTANCE_RINGS,

  COUNT
};

static_assert(unsigned(ControlIndex::COUNT) == PageSettingElementsCount,
              "Elements config controls must match catalog size");

[[nodiscard]]
bool
IsExpertRow(unsigned control) noexcept
{
  switch (ControlIndex(control)) {
  case ControlIndex::TRAIL_LENGTH:
  case ControlIndex::TRAIL_DRIFT:
  case ControlIndex::TRAIL_TYPE:
  case ControlIndex::TRAIL_SCALED:
  case ControlIndex::DETOUR_COST_MARKERS:
  case ControlIndex::AIRCRAFT_SYMBOL:
  case ControlIndex::WIND_ARROW_STYLE:
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
  ElementsDisplaySetting::ApplyLive(bundle);
  ActionInterface::SendMapSettings(true);
}

void
ElementsConfigPanel::ShowTrailControls(bool show)
{
  SetRowVisible(unsigned(ControlIndex::TRAIL_DRIFT), show);
  SetRowVisible(unsigned(ControlIndex::TRAIL_TYPE), show);
  SetRowVisible(unsigned(ControlIndex::TRAIL_SCALED), show);
}

void
ElementsConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  if (IsDataField(unsigned(ControlIndex::TRAIL_LENGTH), df))
    ShowTrailControls(bundle.trail.length != TrailSettings::Length::OFF);

  ApplyBundleLive();
}

void
ElementsConfigPanel::Prepare([[maybe_unused]] ContainerWindow &parent,
                             [[maybe_unused]] const PixelRect &rc) noexcept
{
  ElementsDisplaySetting::ReadLive(bundle);

  DisplaySettingConfigPanel::AddCatalogRows(
    *this, bundle, PageSettingElementsStart, PageSettingElementsCount,
    ElementsDisplaySetting::Get, ElementsDisplaySetting::GetValue,
    IsExpertRow, this, nullptr, unsigned(ControlIndex::TRAIL_LENGTH));

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
