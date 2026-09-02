// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "OrientationConfigPanel.hpp"
#include "Orientation/OrientationDisplaySetting.hpp"
#include "Dialogs/Settings/DisplaySettingConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "ActionInterface.hpp"
#include "Form/DataField/Listener.hpp"
#include "Interface.hpp"
#include "PageSetting.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"

namespace {

enum ControlIndex : unsigned {
  CATALOG_END = PageSettingOrientationCount,

  MaxAutoZoomDistance = CATALOG_END,
  PAGES_DISTINCT_ZOOM,
};

[[nodiscard]]
bool
IsExpertRow(unsigned control) noexcept
{
  return control == unsigned(PageSettingId::MAP_SHIFT_BIAS) -
         PageSettingOrientationStart ||
         control == unsigned(PageSettingId::GLIDER_SCREEN_POSITION) -
         PageSettingOrientationStart;
}

[[nodiscard]]
bool
NeedsListener(unsigned control) noexcept
{
  return control == unsigned(PageSettingId::CRUISE_ORIENTATION) -
         PageSettingOrientationStart ||
         control == unsigned(PageSettingId::CIRCLING_ORIENTATION) -
         PageSettingOrientationStart ||
         control == unsigned(PageSettingId::MAP_SHIFT_BIAS) -
         PageSettingOrientationStart;
}

} // namespace

class OrientationConfigPanel final
  : public RowFormWidget, DataFieldListener {
  OrientationDisplaySetting::Bundle bundle;
  OrientationDisplaySetting::Bundle initial_bundle;

  void SyncBundleFromForm() noexcept;
  void ApplyBundleLive() noexcept;

public:
  OrientationConfigPanel()
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
OrientationConfigPanel::SyncBundleFromForm() noexcept
{
  DisplaySettingConfigPanel::SyncBundleFromForm(
    *this, bundle, PageSettingOrientationStart,
    PageSettingOrientationCount,
    OrientationDisplaySetting::Get, OrientationDisplaySetting::SetValue);
}

void
OrientationConfigPanel::ApplyBundleLive() noexcept
{
  OrientationDisplaySetting::ApplyLive(bundle);
  ActionInterface::SendMapSettings(true);
}

void
OrientationConfigPanel::UpdateVisibilities()
{
  SetRowVisible(unsigned(PageSettingId::MAP_SHIFT_BIAS) -
                PageSettingOrientationStart,
                bundle.cruise_orientation == MapOrientation::NORTH_UP ||
                bundle.cruise_orientation == MapOrientation::WIND_UP);
}

void
OrientationConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  if (IsDataField(unsigned(PageSettingId::CRUISE_ORIENTATION) -
                  PageSettingOrientationStart, df) ||
      IsDataField(unsigned(PageSettingId::CIRCLING_ORIENTATION) -
                  PageSettingOrientationStart, df) ||
      IsDataField(unsigned(PageSettingId::MAP_SHIFT_BIAS) -
                  PageSettingOrientationStart, df))
    UpdateVisibilities();

  ApplyBundleLive();
}

void
OrientationConfigPanel::Prepare(ContainerWindow &parent,
                              const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  OrientationDisplaySetting::ReadLive(bundle);

  const PageSettings &page_settings = CommonInterface::GetUISettings().pages;

  DisplaySettingConfigPanel::AddCatalogRows(
    *this, bundle, PageSettingOrientationStart, PageSettingOrientationCount,
    OrientationDisplaySetting::Get, OrientationDisplaySetting::GetValue,
    IsExpertRow, this, NeedsListener);

  AddFloat(_("Max. auto zoom distance"),
           _("The upper limit for auto zoom distance."),
           "%.0f %s", "%.0f", 20, 250, 10, false,
           UnitGroup::DISTANCE,
           CommonInterface::GetMapSettings().max_auto_zoom_distance);
  SetExpertRow(MaxAutoZoomDistance);

  AddBoolean(_("Distinct page zoom"),
             _("Maintain one map zoom level on each page."),
             page_settings.distinct_zoom);
  SetExpertRow(PAGES_DISTINCT_ZOOM);

  SyncBundleFromForm();
  initial_bundle = bundle;
  UpdateVisibilities();
}

bool
OrientationConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;

  SyncBundleFromForm();
  OrientationDisplaySetting::ApplyLive(bundle);
  changed |= OrientationDisplaySetting::SaveGlobal(bundle, initial_bundle);

  MapSettings &settings_map = CommonInterface::SetMapSettings();
  PageSettings &page_settings = CommonInterface::SetUISettings().pages;

  changed |= SaveValue(MaxAutoZoomDistance, UnitGroup::DISTANCE,
                       ProfileKeys::MaxAutoZoomDistance,
                       settings_map.max_auto_zoom_distance);

  changed |= SaveValue(PAGES_DISTINCT_ZOOM, ProfileKeys::PagesDistinctZoom,
                       page_settings.distinct_zoom);

  _changed |= changed;

  return true;
}

std::unique_ptr<Widget>
CreateOrientationConfigPanel()
{
  return std::make_unique<OrientationConfigPanel>();
}
