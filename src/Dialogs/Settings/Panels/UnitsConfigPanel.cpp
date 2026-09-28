// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "UnitsConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Geo/CoordinateFormat.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Units/Units.hpp"
#include "Units/UnitsStore.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

static constexpr StaticEnumChoice units_speed_list[] = {
  { Unit::STATUTE_MILES_PER_HOUR, "mph" },
  { Unit::KNOTS, N_("knots") },
  { Unit::KILOMETER_PER_HOUR, "km/h" },
  { Unit::METER_PER_SECOND, "m/s" },
  nullptr
};

static constexpr StaticEnumChoice units_distance_list[] = {
  { Unit::STATUTE_MILES, "sm" },
  { Unit::NAUTICAL_MILES, "nm" },
  { Unit::KILOMETER, "km" },
  nullptr
};

static constexpr StaticEnumChoice units_lift_list[] = {
  { Unit::KNOTS, N_("knots") },
  { Unit::METER_PER_SECOND, "m/s" },
  { Unit::FEET_PER_MINUTE, "ft/min" },
  nullptr
};

static constexpr StaticEnumChoice units_altitude_list[] = {
  { Unit::FEET,  N_("feet") },
  { Unit::METER, N_("meters") },
  nullptr
};

static constexpr StaticEnumChoice units_temperature_list[] = {
  { Unit::DEGREES_CELCIUS, DEG "C" },
  { Unit::DEGREES_FAHRENHEIT, DEG "F" },
  nullptr
};

static constexpr StaticEnumChoice units_taskspeed_list[] = {
  { Unit::STATUTE_MILES_PER_HOUR, "mph" },
  { Unit::KNOTS, N_("knots") },
  { Unit::KILOMETER_PER_HOUR, "km/h" },
  { Unit::METER_PER_SECOND, "m/s" },
  nullptr
};

static constexpr StaticEnumChoice pressure_labels_list[] = {
  { Unit::HECTOPASCAL, "hPa" },
  { Unit::MILLIBAR, "mb" },
  { Unit::INCH_MERCURY, "inHg" },
  nullptr
};

static constexpr StaticEnumChoice mass_labels_list[] = {
  { Unit::KG, "kg" },
  { Unit::LB, "lb" },
  nullptr
};

static constexpr StaticEnumChoice wing_loading_labels_list[] = {
  { Unit::KG_PER_M2, "kg/m²" },
  { Unit::LB_PER_FT2, "lb/ft²" },
  nullptr
};

static constexpr StaticEnumChoice units_lat_lon_list[] = {
  { CoordinateFormat::DDMMSS, "DDMMSS" },
  { CoordinateFormat::DDMMSS_S, "DDMMSS.s" },
  { CoordinateFormat::DDMM_MMM, "DDMM.mmm" },
  { CoordinateFormat::DD_DDDDD, "DD.ddddd" },
  { CoordinateFormat::UTM, "UTM" },
  nullptr
};

static constexpr StaticEnumChoice rotation_labels_list[] = {
  { Unit::HZ, "Hz" },
  { Unit::RPM, "rpm" },
  nullptr
};

static unsigned
MatchingPreset(const UnitSetting &units) noexcept
{
  UnitSetting current = units;
  current.wind_speed_unit = current.speed_unit;
  return Units::Store::EqualsPresetUnits(current);
}

static const char *
PresetLabel(const UnitSetting &units) noexcept
{
  const unsigned preset = MatchingPreset(units);
  if (preset == 0)
    return _("Custom");

  return Units::Store::GetName(preset - 1);
}

static void
ApplyPreset(UnitSetting &units, unsigned preset) noexcept
{
  if (preset == 0)
    return;

  const UnitSetting &from = Units::Store::Read(preset - 1);
  units.speed_unit = from.speed_unit;
  units.wind_speed_unit = from.speed_unit;
  units.distance_unit = from.distance_unit;
  units.vertical_speed_unit = from.vertical_speed_unit;
  units.altitude_unit = from.altitude_unit;
  units.temperature_unit = from.temperature_unit;
  units.task_speed_unit = from.task_speed_unit;
  units.pressure_unit = from.pressure_unit;
  units.mass_unit = from.mass_unit;
  units.wing_loading_unit = from.wing_loading_unit;
}

std::unique_ptr<Widget>
CreateUnitsConfigPanel()
{
  const UISettings &ui = CommonInterface::GetUISettings();

  struct Fields {
    UnitSetting units;
    CoordinateFormat coordinate_format;
  };

  auto fields = std::make_shared<Fields>(Fields{
    ui.format.units,
    ui.format.coordinate_format,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();
  list->AddGroup(nullptr);
  list->AddValue(_("Preset"), _("Load a set of units."),
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = PresetLabel(fields->units);
                 },
                 [fields, page] {
                   DataFieldEnum df;
                   df.addEnumText(_("Custom"), 0u,
                                  _("My individual set of units."));
                   const unsigned len = Units::Store::Count();
                   for (unsigned i = 0; i < len; ++i)
                     df.addEnumText(Units::Store::GetName(i), i + 1);

                   df.SetValue(MatchingPreset(fields->units));
                   if (!EditDataFieldDialog(_("Preset"), df,
                                            _("Load a set of units.")))
                     return;

                   ApplyPreset(fields->units, df.GetValue());
                   page->UpdateValues();
                 });

  list->AddEnum(_("Aircraft/Wind speed"),
                _("Units used for airspeed and ground speed. "
                  "A separate unit is available for task speeds."),
                units_speed_list, fields->units.speed_unit, true);
  list->AddEnum(_("Distance"),
                _("Units used for horizontal distances e.g. "
                  "range to waypoint, distance to go."),
                units_distance_list, fields->units.distance_unit, true);
  list->AddEnum(_("Lift"),
                _("Units used for vertical speeds (variometer)."),
                units_lift_list, fields->units.vertical_speed_unit, true);
  list->AddEnum(_("Altitude"),
                _("Units used for altitude and heights."),
                units_altitude_list, fields->units.altitude_unit, true);
  list->AddEnum(_("Temperature"), _("Units used for temperature."),
                units_temperature_list, fields->units.temperature_unit, true);
  list->AddEnum(_("Task Speed"), _("Units used for task speeds."),
                units_taskspeed_list, fields->units.task_speed_unit, true);
  list->AddEnum(_("Pressure"), _("Units used for pressures."),
                pressure_labels_list, fields->units.pressure_unit, true);
  list->AddEnum(_("Mass"), _("Units used for mass."),
                mass_labels_list, fields->units.mass_unit, true);
  list->AddEnum(_("Wing loading"), _("Units used for wing loading."),
                wing_loading_labels_list, fields->units.wing_loading_unit,
                true);
  list->AddEnum(_("Lat./Lon."),
                _("Units used for latitude and longitude."),
                units_lat_lon_list, fields->coordinate_format, true);
  list->AddEnum(_("Rotation"), _("Unit used for rotation."),
                rotation_labels_list, fields->units.rotation_unit, true);

  list->SetSaveCallback([fields](bool &changed) {
    UnitSetting &config = CommonInterface::SetUISettings().format.units;
    CoordinateFormat &coordinate_format =
      CommonInterface::SetUISettings().format.coordinate_format;

    /* the Units settings affect how other form values are read and translated
     * so changes to Units settings should be processed after all other form settings
     */
    ConfigPanel::CommitSetting(changed, config.speed_unit,
                               fields->units.speed_unit,
                               ProfileKeys::SpeedUnitsValue);
    config.wind_speed_unit = config.speed_unit;

    ConfigPanel::CommitSetting(changed, config.distance_unit,
                               fields->units.distance_unit,
                               ProfileKeys::DistanceUnitsValue);
    ConfigPanel::CommitSetting(changed, config.vertical_speed_unit,
                               fields->units.vertical_speed_unit,
                               ProfileKeys::LiftUnitsValue);
    ConfigPanel::CommitSetting(changed, config.altitude_unit,
                               fields->units.altitude_unit,
                               ProfileKeys::AltitudeUnitsValue);
    ConfigPanel::CommitSetting(changed, config.temperature_unit,
                               fields->units.temperature_unit,
                               ProfileKeys::TemperatureUnitsValue);
    ConfigPanel::CommitSetting(changed, config.task_speed_unit,
                               fields->units.task_speed_unit,
                               ProfileKeys::TaskSpeedUnitsValue);
    ConfigPanel::CommitSetting(changed, config.pressure_unit,
                               fields->units.pressure_unit,
                               ProfileKeys::PressureUnitsValue);
    ConfigPanel::CommitSetting(changed, config.mass_unit,
                               fields->units.mass_unit,
                               ProfileKeys::MassUnitValue);
    ConfigPanel::CommitSetting(changed, config.wing_loading_unit,
                               fields->units.wing_loading_unit,
                               ProfileKeys::WingLoadingUnitValue);
    ConfigPanel::CommitSetting(changed, coordinate_format,
                               fields->coordinate_format,
                               ProfileKeys::LatLonUnits);
    ConfigPanel::CommitSetting(changed, config.rotation_unit,
                               fields->units.rotation_unit,
                               ProfileKeys::RotationUnitValue);
    return true;
  });

  return list;
}
