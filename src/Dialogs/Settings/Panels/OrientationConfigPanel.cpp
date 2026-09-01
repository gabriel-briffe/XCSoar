// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "OrientationConfigPanel.hpp"
#include "Orientation/OrientationDisplaySetting.hpp"
#include "DisplaySettingConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "ActionInterface.hpp"
#include "Form/DataField/Listener.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"

enum ControlIndex {
  OrientationCruise,
  OrientationCircling,
  CirclingZoom,
  MAP_SHIFT_BIAS,
  GliderScreenPosition,
  MaxAutoZoomDistance,
  PAGES_DISTINCT_ZOOM,
};

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
  using Id = PageSettingId;

  OrientationDisplaySetting::SetValue(bundle, Id::CRUISE_ORIENTATION,
                                      int(GetValueEnum(OrientationCruise)));
  OrientationDisplaySetting::SetValue(bundle, Id::CIRCLING_ORIENTATION,
                                      int(GetValueEnum(OrientationCircling)));
  OrientationDisplaySetting::SetValue(bundle, Id::CIRCLING_ZOOM,
                                      GetValueBoolean(CirclingZoom) ? 1 : 0);
  OrientationDisplaySetting::SetValue(bundle, Id::MAP_SHIFT_BIAS,
                                      int(GetValueEnum(MAP_SHIFT_BIAS)));
  OrientationDisplaySetting::SetValue(bundle, Id::GLIDER_SCREEN_POSITION,
                                      GetValueInteger(GliderScreenPosition));
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
  SetRowVisible(MAP_SHIFT_BIAS,
                bundle.cruise_orientation == MapOrientation::NORTH_UP ||
                bundle.cruise_orientation == MapOrientation::WIND_UP);
}

void
OrientationConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  if (IsDataField(OrientationCruise, df) ||
      IsDataField(OrientationCircling, df) ||
      IsDataField(MAP_SHIFT_BIAS, df))
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

  DisplaySettingConfigPanel::AddEnumRow(
    *this,
    OrientationDisplaySetting::Get(PageSettingId::CRUISE_ORIENTATION),
    unsigned(bundle.cruise_orientation), this);

  DisplaySettingConfigPanel::AddEnumRow(
    *this,
    OrientationDisplaySetting::Get(PageSettingId::CIRCLING_ORIENTATION),
    unsigned(bundle.circling_orientation), this);

  DisplaySettingConfigPanel::AddBooleanRow(
    *this,
    OrientationDisplaySetting::Get(PageSettingId::CIRCLING_ZOOM),
    bundle.circle_zoom_enabled);

  DisplaySettingConfigPanel::AddEnumRow(
    *this,
    OrientationDisplaySetting::Get(PageSettingId::MAP_SHIFT_BIAS),
    unsigned(bundle.map_shift_bias), this);
  SetExpertRow(MAP_SHIFT_BIAS);

  DisplaySettingConfigPanel::AddIntegerRow(
    *this,
    OrientationDisplaySetting::Get(PageSettingId::GLIDER_SCREEN_POSITION),
    bundle.glider_screen_position);
  SetExpertRow(GliderScreenPosition);

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
