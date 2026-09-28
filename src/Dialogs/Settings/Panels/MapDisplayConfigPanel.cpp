// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "MapDisplayConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StaticString.hxx"

#include <cmath>
#include <memory>
template<typename T>
static void
AddLinkedEnum(GroupedListWidget &list, const char *caption,
              const char *help, const StaticEnumChoice *choices,
              T &value) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.value_callback =
    [choices, &value](GroupedListWidget::ValueState &state) {
      state.text.clear();
      for (auto i = choices; i->display_string != nullptr; ++i)
        if (i->id == static_cast<unsigned>(value))
          state.text = gettext(i->display_string);
    };
  list.AddValue(caption, [&list, caption, help, choices, &value] {
    DataFieldEnum df;
    df.EnableItemHelp(choices->help != nullptr);
    df.AddChoices(choices);
    df.SetValue(static_cast<unsigned>(value));
    if (!ComboPicker(caption, df, help) ||
        df.GetValue() == static_cast<unsigned>(value))
      return;
    value = static_cast<T>(df.GetValue());
    if (list.UpdateValues())
      list.UpdateLayout();
  }, std::move(options));
}

static constexpr StaticEnumChoice orientation_list[] = {
  { MapOrientation::TRACK_UP, N_("Track up"),
    N_("The moving map display will be rotated so the glider's track is oriented up.") },
  { MapOrientation::HEADING_UP, N_("Heading up"),
    N_("The moving map display will be rotated so the glider's heading is oriented up.") },
  { MapOrientation::NORTH_UP, N_("North up"),
    N_("The moving map display will always be orientated north to south and the glider icon will be rotated to show its course.") },
  { MapOrientation::TARGET_UP, N_("Target up"),
    N_("The moving map display will be rotated so the navigation target is oriented up.") },
  { MapOrientation::WIND_UP, N_("Wind up"),
    N_("The moving map display will be rotated so the wind is always oriented up to down. (can be useful for wave flying)") },
  nullptr
};

static constexpr StaticEnumChoice shift_bias_list[] = {
  { MapShiftBias::NONE, N_("None"), N_("Disable adjustments.") },
  { MapShiftBias::TRACK, N_("Track"),
    N_("Use a recent average of the ground track as basis.") },
  { MapShiftBias::TARGET, N_("Target"),
    N_("Use the current target waypoint as basis.") },
  nullptr
};

std::unique_ptr<Widget>
CreateMapDisplayConfigPanel()
{
  const MapSettings &settings_map = CommonInterface::GetMapSettings();
  const PageSettings &page_settings =
    CommonInterface::GetUISettings().pages;

  struct Fields {
    MapOrientation cruise;
    MapOrientation circling;
    bool circle_zoom;
    MapShiftBias shift_bias;
    int glider_position;
    double max_auto_zoom;
    bool distinct_zoom;
    StaticString<24> zoom_format;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings_map.cruise_orientation,
    settings_map.circling_orientation,
    settings_map.circle_zoom_enabled,
    settings_map.map_shift_bias,
    settings_map.glider_screen_position,
    settings_map.max_auto_zoom_distance,
    page_settings.distinct_zoom,
    {},
  });

  const Unit distance_unit =
    Units::GetUserUnitByGroup(UnitGroup::DISTANCE);
  fields->max_auto_zoom =
    Units::ToUserUnit(fields->max_auto_zoom, distance_unit);
  fields->zoom_format.Format("%%.0f %s",
                             Units::GetUnitName(distance_unit));

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  AddLinkedEnum(*list, _("Cruise orientation"),
                _("Determines how the screen is rotated with the glider"),
                orientation_list, fields->cruise);
  list->AddEnum(_("Circling orientation"),
                _("Determines how the screen is rotated with the glider while circling"),
                orientation_list, fields->circling);
  list->AddSwitch(_("Circling zoom"),
                  _("If enabled, then the map will zoom in automatically when entering circling mode and zoom out automatically when leaving circling mode."),
                  fields->circle_zoom);
  list->AddEnum(_("Map shift reference"),
                _("Determines what is used to shift the glider from the map center"),
                shift_bias_list, fields->shift_bias, true,
                [fields] {
                  return fields->cruise == MapOrientation::NORTH_UP ||
                         fields->cruise == MapOrientation::WIND_UP;
                });
  list->AddInteger(_("Glider position offset"),
                   _("Defines the location of the glider drawn on the screen in percent from the screen edge."),
                   "%d %%", "%d", 10, 50, 5,
                   fields->glider_position, true);
  list->AddFloat(_("Max. auto zoom distance"),
                 _("The upper limit for auto zoom distance."),
                 fields->zoom_format.c_str(), "%.0f",
                 20, 250, 10, false, fields->max_auto_zoom, true);
  list->AddSwitch(_("Distinct page zoom"),
                  _("Maintain one map zoom level on each page."),
                  fields->distinct_zoom, true);

  list->SetSaveCallback([fields](bool &changed) {
    MapSettings &settings_map = CommonInterface::SetMapSettings();
    PageSettings &page_settings = CommonInterface::SetUISettings().pages;

    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.cruise_orientation, fields->cruise,
      ProfileKeys::OrientationCruise);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.circling_orientation, fields->circling,
      ProfileKeys::OrientationCircling);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.map_shift_bias, fields->shift_bias,
      ProfileKeys::MapShiftBias);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.glider_screen_position,
      fields->glider_position, ProfileKeys::GliderScreenPosition);
    changed |= ConfigPanel::CommitSetting(
      changed, settings_map.circle_zoom_enabled, fields->circle_zoom,
      ProfileKeys::CircleZoom);

    const Unit unit = Units::GetUserUnitByGroup(UnitGroup::DISTANCE);
    if (std::fabs(fields->max_auto_zoom -
                  Units::ToUserUnit(settings_map.max_auto_zoom_distance,
                                    unit)) >= 0.1)
      ConfigPanel::CommitSetting(
        changed, settings_map.max_auto_zoom_distance,
        Units::ToSysUnit(fields->max_auto_zoom, unit),
        ProfileKeys::MaxAutoZoomDistance);

    changed |= ConfigPanel::CommitSetting(
      changed, page_settings.distinct_zoom, fields->distinct_zoom,
      ProfileKeys::PagesDistinctZoom);
    return true;
  });

  return list;
}
