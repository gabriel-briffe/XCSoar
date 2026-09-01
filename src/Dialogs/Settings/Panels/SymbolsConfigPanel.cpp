// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SymbolsConfigPanel.hpp"
#include "Elements/ElementsDisplaySetting.hpp"
#include "DisplaySettingConfigPanel.hpp"
#include "ActionInterface.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Listener.hpp"
#include "Interface.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"

enum ControlIndex {
  DISPLAY_TRACK_BEARING,
  ENABLE_FLARM_MAP,
  FADE_TRAFFIC,
  TRAIL_LENGTH,
  TRAIL_DRIFT,
  TRAIL_TYPE,
  TRAIL_WIDTH,
  ENABLE_DETOUR_COST_MARKERS,
  AIRCRAFT_SYMBOL,
  WIND_ARROW_STYLE,
  SKYLINES_TRAFFIC_MAP_MODE,
  DISTANCE_RINGS_ENABLED,
};

class SymbolsConfigPanel final
  : public RowFormWidget, DataFieldListener {
  ElementsDisplaySetting::Bundle bundle;
  ElementsDisplaySetting::Bundle initial_bundle;

  void SyncBundleFromForm() noexcept;
  void ApplyBundleLive() noexcept;

public:
  SymbolsConfigPanel()
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
SymbolsConfigPanel::SyncBundleFromForm() noexcept
{
  using Id = PageSettingId;

  ElementsDisplaySetting::SetValue(bundle, Id::GROUND_TRACK,
                                 int(GetValueEnum(DISPLAY_TRACK_BEARING)));
  ElementsDisplaySetting::SetValue(bundle, Id::FLARM_TRAFFIC,
                                 GetValueBoolean(ENABLE_FLARM_MAP) ? 1 : 0);
  ElementsDisplaySetting::SetValue(bundle, Id::FADE_TRAFFIC,
                                 GetValueBoolean(FADE_TRAFFIC) ? 1 : 0);
  ElementsDisplaySetting::SetValue(bundle, Id::TRAIL_LENGTH,
                                 int(GetValueEnum(TRAIL_LENGTH)));
  ElementsDisplaySetting::SetValue(bundle, Id::TRAIL_DRIFT,
                                 GetValueBoolean(TRAIL_DRIFT) ? 1 : 0);
  ElementsDisplaySetting::SetValue(bundle, Id::TRAIL_TYPE,
                                 int(GetValueEnum(TRAIL_TYPE)));
  ElementsDisplaySetting::SetValue(bundle, Id::TRAIL_SCALED,
                                 GetValueBoolean(TRAIL_WIDTH) ? 1 : 0);
  ElementsDisplaySetting::SetValue(bundle, Id::DETOUR_COST_MARKERS,
                                 GetValueBoolean(ENABLE_DETOUR_COST_MARKERS)
                                   ? 1 : 0);
  ElementsDisplaySetting::SetValue(bundle, Id::AIRCRAFT_SYMBOL,
                                 int(GetValueEnum(AIRCRAFT_SYMBOL)));
  ElementsDisplaySetting::SetValue(bundle, Id::WIND_ARROW_STYLE,
                                 int(GetValueEnum(WIND_ARROW_STYLE)));
  ElementsDisplaySetting::SetValue(bundle, Id::ONLINE_TRAFFIC_MAP_MODE,
                                 int(GetValueEnum(SKYLINES_TRAFFIC_MAP_MODE)));
  ElementsDisplaySetting::SetValue(bundle, Id::DISTANCE_RINGS,
                                 GetValueBoolean(DISTANCE_RINGS_ENABLED)
                                   ? 1 : 0);
}

void
SymbolsConfigPanel::ApplyBundleLive() noexcept
{
  ElementsDisplaySetting::ApplyLive(bundle);
  ActionInterface::SendMapSettings(true);
}

void
SymbolsConfigPanel::ShowTrailControls(bool show)
{
  SetRowVisible(TRAIL_DRIFT, show);
  SetRowVisible(TRAIL_TYPE, show);
  SetRowVisible(TRAIL_WIDTH, show);
}

void
SymbolsConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  if (IsDataField(TRAIL_LENGTH, df))
    ShowTrailControls(bundle.trail.length != TrailSettings::Length::OFF);

  ApplyBundleLive();
}

void
SymbolsConfigPanel::Prepare([[maybe_unused]] ContainerWindow &parent,
                            [[maybe_unused]] const PixelRect &rc) noexcept
{
  ElementsDisplaySetting::ReadLive(bundle);

  DisplaySettingConfigPanel::AddRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::GROUND_TRACK),
    ElementsDisplaySetting::GetValue(bundle, PageSettingId::GROUND_TRACK));

  DisplaySettingConfigPanel::AddBooleanRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::FLARM_TRAFFIC),
    bundle.show_flarm_on_map);

  DisplaySettingConfigPanel::AddBooleanRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::FADE_TRAFFIC),
    bundle.fade_traffic);

  DisplaySettingConfigPanel::AddEnumRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::TRAIL_LENGTH),
    unsigned(bundle.trail.length), this);
  SetExpertRow(TRAIL_LENGTH);

  DisplaySettingConfigPanel::AddBooleanRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::TRAIL_DRIFT),
    bundle.trail.wind_drift_enabled);
  SetExpertRow(TRAIL_DRIFT);

  DisplaySettingConfigPanel::AddEnumRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::TRAIL_TYPE),
    unsigned(bundle.trail.type));
  SetExpertRow(TRAIL_TYPE);

  DisplaySettingConfigPanel::AddBooleanRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::TRAIL_SCALED),
    bundle.trail.scaling_enabled);
  SetExpertRow(TRAIL_WIDTH);

  DisplaySettingConfigPanel::AddBooleanRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::DETOUR_COST_MARKERS),
    bundle.detour_cost_markers_enabled);
  SetExpertRow(ENABLE_DETOUR_COST_MARKERS);

  DisplaySettingConfigPanel::AddEnumRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::AIRCRAFT_SYMBOL),
    unsigned(bundle.aircraft_symbol));
  SetExpertRow(AIRCRAFT_SYMBOL);

  DisplaySettingConfigPanel::AddEnumRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::WIND_ARROW_STYLE),
    unsigned(bundle.wind_arrow_style));
  SetExpertRow(WIND_ARROW_STYLE);

  DisplaySettingConfigPanel::AddEnumRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::ONLINE_TRAFFIC_MAP_MODE),
    unsigned(bundle.online_traffic_map_mode));

  DisplaySettingConfigPanel::AddBooleanRow(
    *this,
    ElementsDisplaySetting::Get(PageSettingId::DISTANCE_RINGS),
    bundle.distance_rings_enabled);

  SyncBundleFromForm();
  initial_bundle = bundle;
  ShowTrailControls(bundle.trail.length != TrailSettings::Length::OFF);
}

bool
SymbolsConfigPanel::Save(bool &_changed) noexcept
{
  SyncBundleFromForm();
  ElementsDisplaySetting::ApplyLive(bundle);
  _changed |= ElementsDisplaySetting::SaveGlobal(bundle, initial_bundle);
  return true;
}

std::unique_ptr<Widget>
CreateSymbolsConfigPanel()
{
  return std::make_unique<SymbolsConfigPanel>();
}
