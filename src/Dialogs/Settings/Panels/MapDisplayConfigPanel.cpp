// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "MapDisplayConfigPanel.hpp"
#include "MapDisplaySetting.hpp"
#include "Profile/Keys.hpp"
#include "ActionInterface.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Listener.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MapDisplayChoices.hpp"
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

class MapDisplayConfigPanel final
  : public RowFormWidget, DataFieldListener {
  MapDisplaySetting::Bundle bundle;
  MapDisplaySetting::Bundle initial_bundle;

  void SyncBundleFromForm() noexcept;
  void ApplyBundleLive() noexcept;

public:
  MapDisplayConfigPanel()
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
MapDisplayConfigPanel::SyncBundleFromForm() noexcept
{
  using Id = PageSettingId;

  MapDisplaySetting::SetValue(bundle, Id::CRUISE_ORIENTATION,
                              int(GetValueEnum(OrientationCruise)));
  MapDisplaySetting::SetValue(bundle, Id::CIRCLING_ORIENTATION,
                              int(GetValueEnum(OrientationCircling)));
  MapDisplaySetting::SetValue(bundle, Id::CIRCLING_ZOOM,
                              GetValueBoolean(CirclingZoom) ? 1 : 0);
  MapDisplaySetting::SetValue(bundle, Id::MAP_SHIFT_BIAS,
                              int(GetValueEnum(MAP_SHIFT_BIAS)));
  MapDisplaySetting::SetValue(bundle, Id::GLIDER_SCREEN_POSITION,
                              GetValueInteger(GliderScreenPosition));
}

void
MapDisplayConfigPanel::ApplyBundleLive() noexcept
{
  MapDisplaySetting::ApplyLive(bundle);
  ActionInterface::SendMapSettings(true);
}

void
MapDisplayConfigPanel::UpdateVisibilities()
{
  SetRowVisible(MAP_SHIFT_BIAS,
                bundle.cruise_orientation == MapOrientation::NORTH_UP ||
                bundle.cruise_orientation == MapOrientation::WIND_UP);
}

void
MapDisplayConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  if (IsDataField(OrientationCruise, df) ||
      IsDataField(OrientationCircling, df) ||
      IsDataField(MAP_SHIFT_BIAS, df))
    UpdateVisibilities();

  ApplyBundleLive();
}

void
MapDisplayConfigPanel::Prepare(ContainerWindow &parent,
                               const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  MapDisplaySetting::ReadLive(bundle);

  const PageSettings &page_settings = CommonInterface::GetUISettings().pages;

  const auto &cruise =
    MapDisplaySetting::Get(PageSettingId::CRUISE_ORIENTATION);
  AddEnum(gettext(cruise.label), gettext(cruise.help_global),
          cruise.choices,
          unsigned(bundle.cruise_orientation),
          this);

  const auto &circling =
    MapDisplaySetting::Get(PageSettingId::CIRCLING_ORIENTATION);
  AddEnum(gettext(circling.label), gettext(circling.help_global),
          circling.choices,
          unsigned(bundle.circling_orientation),
          this);

  const auto &circling_zoom =
    MapDisplaySetting::Get(PageSettingId::CIRCLING_ZOOM);
  AddBoolean(gettext(circling_zoom.label),
             gettext(circling_zoom.help_global),
             bundle.circle_zoom_enabled);

  const auto &shift =
    MapDisplaySetting::Get(PageSettingId::MAP_SHIFT_BIAS);
  AddEnum(gettext(shift.label), gettext(shift.help_global),
          shift.choices,
          unsigned(bundle.map_shift_bias),
          this);
  SetExpertRow(MAP_SHIFT_BIAS);

  const auto &glider =
    MapDisplaySetting::Get(PageSettingId::GLIDER_SCREEN_POSITION);
  AddInteger(gettext(glider.label), gettext(glider.help_global),
             "%d %%", "%d", glider.int_min, glider.int_max, glider.int_step,
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
MapDisplayConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;

  SyncBundleFromForm();
  MapDisplaySetting::ApplyLive(bundle);
  changed |= MapDisplaySetting::SaveGlobal(bundle, initial_bundle);

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
CreateMapDisplayConfigPanel()
{
  return std::make_unique<MapDisplayConfigPanel>();
}
