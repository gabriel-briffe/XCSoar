// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WaypointDisplayConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Dialogs/Waypoint/WaypointDialogs.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

/* AddSwitch does not re-read shown() on the other rows. */
static void
AddLinkedSwitch(GroupedListWidget &list, const char *caption,
                const char *help, bool &field, bool expert) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.toggle = true;
  options.checked = field;
  options.help = help;
  options.expert = expert;
  list.AddItem(caption, [&list, &field] {
    field = !field;
    if (list.UpdateValues())
      list.UpdateLayout();
  }, options);
}

std::unique_ptr<Widget>
CreateWaypointDisplayConfigPanel()
{
  const WaypointRendererSettings &settings =
    CommonInterface::GetMapSettings().waypoint;

  struct Fields {
    WaypointRendererSettings::DisplayTextType display_text_type;
    WaypointRendererSettings::ArrivalHeightDisplay arrival_height;
    LabelShape label_style;
    WaypointRendererSettings::LabelSelection label_selection;
    WaypointRendererSettings::LandableStyle landable_style;
    int icon_scale;
    bool detailed_landables;
    int landable_scale;
    bool scale_runway;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings.display_text_type,
    settings.arrival_height_display,
    settings.landable_render_mode,
    settings.label_selection,
    settings.landable_style,
    settings.map_waypoint_icon_scale,
    settings.vector_landable_rendering,
    settings.landable_rendering_scale,
    settings.scale_runway_length,
  });

  const auto detailed = [fields] {
    return fields->detailed_landables;
  };

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

  static constexpr StaticEnumChoice wp_labels_list[] = {
    { WaypointRendererSettings::DisplayTextType::NAME,
      N_("Full name"),
      N_("The full name of each waypoint is displayed.") },
    { WaypointRendererSettings::DisplayTextType::FIRST_WORD,
      N_("First word of name"),
      N_("The first word of the waypoint name is displayed.") },
    { WaypointRendererSettings::DisplayTextType::FIRST_THREE,
      N_("First 3 letters"),
      N_("The first 3 letters of the waypoint name are displayed.") },
    { WaypointRendererSettings::DisplayTextType::FIRST_FIVE,
      N_("First 5 letters"),
      N_("The first 5 letters of the waypoint name are displayed.") },
    { WaypointRendererSettings::DisplayTextType::NONE,
      N_("None"), N_("No waypoint name is displayed.") },
    { WaypointRendererSettings::DisplayTextType::SHORT_NAME,
      N_("Short Name"),
      N_("The short name of each waypoint is displayed. If unavailable, the first five letters of the full name are displayed.") },
    nullptr
  };
  list->AddEnum(_("Label format"),
                _("Determines how labels are displayed with each waypoint"),
                wp_labels_list, fields->display_text_type);

  static constexpr StaticEnumChoice wp_arrival_list[] = {
    { WaypointRendererSettings::ArrivalHeightDisplay::NONE,
      N_("None"),
      N_("No arrival height is displayed.") },
    { WaypointRendererSettings::ArrivalHeightDisplay::GLIDE,
      N_("Straight glide"),
      N_("Straight glide arrival height (no terrain is considered).") },
    { WaypointRendererSettings::ArrivalHeightDisplay::TERRAIN,
      N_("Terrain avoidance glide"),
      N_("Arrival height considering terrain avoidance. "
         "Requires \"Reach mode: Turning\" in \"Glide Computer > Route\" settings.") },
    { WaypointRendererSettings::ArrivalHeightDisplay::GLIDE_AND_TERRAIN,
      N_("Straight & terrain glide"),
      N_("Both arrival heights are displayed. "
         "Requires \"Reach mode: Turning\" in \"Glide Computer > Route\" settings.") },
    { WaypointRendererSettings::ArrivalHeightDisplay::REQUIRED_GR,
      N_("Required glide ratio") },
    { WaypointRendererSettings::ArrivalHeightDisplay::REQUIRED_GR_AND_TERRAIN,
      N_("Required GR & terrain glide"),
      N_("Both Required glide ratio and terrain avoidance height are displayed. "
         "Requires \"Reach mode: Turning\" in \"Glide Computer > Route\" settings.") },
    nullptr
  };

  list->AddEnum(_("Arrival height"),
                _("Determines how arrival height is displayed in waypoint labels"),
                wp_arrival_list, fields->arrival_height, true);

  static constexpr StaticEnumChoice wp_label_list[] = {
    { LabelShape::ROUNDED_BLACK, N_("Rounded rectangle") },
    { LabelShape::OUTLINED_INVERTED, N_("Outlined") },
    nullptr
  };

  list->AddEnum(_("Label style"), nullptr, wp_label_list,
                fields->label_style, true);

  static constexpr StaticEnumChoice wp_selection_list[] = {
    { WaypointRendererSettings::LabelSelection::ALL,
      N_("All"), N_("All labels will be displayed.") },
    { WaypointRendererSettings::LabelSelection::TASK_AND_AIRFIELD,
      N_("Task waypoints & airfields"),
      N_("All waypoints part of a task and all airfields will be displayed.") },
    { WaypointRendererSettings::LabelSelection::TASK_AND_LANDABLE,
      N_("Task waypoints & landables"),
      N_("All waypoints part of a task and all landables will be displayed.") },
    { WaypointRendererSettings::LabelSelection::TASK,
      N_("Task waypoints"),
      N_("All waypoints part of a task will be displayed.") },
    { WaypointRendererSettings::LabelSelection::NONE,
      N_("None"), N_("No labels will be displayed.") },
    nullptr
  };

  list->AddEnum(_("Label visibility"),
                _("Determines what labels are displayed."),
                wp_selection_list, fields->label_selection, true);

  static constexpr StaticEnumChoice wp_style_list[] = {
    { WaypointRendererSettings::LandableStyle::PURPLE_CIRCLE,
      N_("Purple circle"),
      N_("Airports and outlanding fields are displayed as purple circles. If the waypoint is "
          "reachable a bigger green circle is added behind the purple one. If the waypoint is "
          "blocked by a mountain the green circle will be red instead.") },
    { WaypointRendererSettings::LandableStyle::BW,
      N_("B/W"),
      N_("Airports and outlanding fields are displayed in white/grey. If the waypoint is "
          "reachable the color is changed to green. If the waypoint is blocked by a mountain "
          "the color is changed to red instead.") },
    { WaypointRendererSettings::LandableStyle::TRAFFIC_LIGHTS,
      N_("Traffic lights"),
      N_("Airports and outlanding fields are displayed in the colors of a traffic light. "
          "Green if reachable, Orange if blocked by mountain and red if not reachable at all.") },
    { WaypointRendererSettings::LandableStyle::PURPLE_CIRCLE_ONLY,
      N_("Purple circle only"),
      N_("Airports and outlanding fields are displayed as purple circles only. "
          "Reachability is not calculated or shown for landables.") },
    nullptr
  };
  list->AddEnum(_("Landable symbols"),
                _("Purple circles (WinPilot style), high-contrast monochrome, traffic lights, "
                  "or purple circles without reach marking. The first three styles mark "
                  "waypoints within reach green."),
                wp_style_list, fields->landable_style);
  list->AddInteger(_("Waypoint icon size"),
                   _("Size of waypoint symbols on the map as a percentage of the "
                     "built-in artwork (list dialogs keep a fixed row icon size)."),
                   "%u %%", "%u", 50, 200, 10, fields->icon_scale);
  AddLinkedSwitch(*list, _("Detailed landables"),
                  _("[Off] Display fixed icons for landables.\n"
                    "[On] Show landables with variable information like runway length and heading."),
                  fields->detailed_landables, true);
  list->AddInteger(_("Landable size"),
                   _("A percentage to select the size landables are displayed on the map."),
                   "%u %%", "%u", 50, 200, 10, fields->landable_scale,
                   true, detailed);
  list->AddSwitch(_("Scale runway length"),
                  _("[Off] Display fixed length for runways.\n"
                    "[On] Scale displayed runway length based on real length."),
                  fields->scale_runway, true, detailed);

  list->SetVisibilityCallback([](bool visible) {
    if (visible)
      ConfigPanel::BorrowExtraButton(2, _("Filter"), [](){
        dlgWaypointFilterShowModal();
      });
    else
      ConfigPanel::ReturnExtraButton(2);
  });

  list->SetSaveCallback([fields](bool &changed) {
    WaypointRendererSettings &settings =
      CommonInterface::SetMapSettings().waypoint;

    changed |= ConfigPanel::CommitSetting(
      changed, settings.display_text_type, fields->display_text_type,
      ProfileKeys::DisplayText);
    changed |= ConfigPanel::CommitSetting(
      changed, settings.arrival_height_display, fields->arrival_height,
      ProfileKeys::WaypointArrivalHeightDisplay);
    changed |= ConfigPanel::CommitSetting(
      changed, settings.landable_render_mode, fields->label_style,
      ProfileKeys::WaypointLabelStyle);
    changed |= ConfigPanel::CommitSetting(
      changed, settings.label_selection, fields->label_selection,
      ProfileKeys::WaypointLabelSelection);
    changed |= ConfigPanel::CommitSetting(
      changed, settings.landable_style, fields->landable_style,
      ProfileKeys::AppIndLandable);
    changed |= ConfigPanel::CommitSetting(
      changed, settings.map_waypoint_icon_scale, fields->icon_scale,
      ProfileKeys::MapWaypointIconScale);
    changed |= ConfigPanel::CommitSetting(
      changed, settings.vector_landable_rendering, fields->detailed_landables,
      ProfileKeys::AppUseSWLandablesRendering);
    changed |= ConfigPanel::CommitSetting(
      changed, settings.landable_rendering_scale, fields->landable_scale,
      ProfileKeys::AppLandableRenderingScale);
    changed |= ConfigPanel::CommitSetting(
      changed, settings.scale_runway_length, fields->scale_runway,
      ProfileKeys::AppScaleRunwayLength);
    return true;
  });

  return list;
}
