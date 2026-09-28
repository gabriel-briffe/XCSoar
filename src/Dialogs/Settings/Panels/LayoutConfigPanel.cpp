// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "LayoutConfigPanel.hpp"
#include "Asset.hpp"
#include "ConfigPanel.hpp"
#include "InfoBoxes/InfoBoxGeometryList.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MainWindow.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

static constexpr StaticEnumChoice tabdialog_style_list[] = {
  { DialogSettings::TabStyle::Text, N_("Text"),
    N_("Show text on tabbed dialogs.") },
  { DialogSettings::TabStyle::Icon, N_("Icons"),
    N_("Show icons on tabbed dialogs.")},
  nullptr
};

static constexpr StaticEnumChoice popup_msg_position_list[] = {
  { UISettings::PopupMessagePosition::CENTER, N_("Center"),
    N_("Center the status message boxes.") },
  { UISettings::PopupMessagePosition::TOP_LEFT, N_("Top left"),
    N_("Show status message boxes in the top left corner.") },
  nullptr
};

static constexpr StaticEnumChoice infobox_border_list[] = {
  { InfoBoxSettings::BorderStyle::BOX,
    N_("Box"), N_("Draws boxes around each InfoBox.") },
  { InfoBoxSettings::BorderStyle::TAB,
    N_("Tab"), N_("Draws a tab at the top of the InfoBox across the title.") },
  { InfoBoxSettings::BorderStyle::SHADED,
    N_("Shaded"), nullptr /* TODO: help text */ },
  { InfoBoxSettings::BorderStyle::GLASS,
    N_("Glass"), nullptr /* TODO: help text */ },
  nullptr
};

static constexpr StaticEnumChoice infobox_theme_list[] = {
  { InfoBoxSettings::Theme::FOLLOW_GLOBAL, N_("Follow global"),
    N_("Use the same light/dark mode as the overall UI.") },
  { InfoBoxSettings::Theme::LIGHT, N_("Light"),
    N_("Always use dark text on a light InfoBox background.") },
  { InfoBoxSettings::Theme::DARK, N_("Dark"),
    N_("Always use light text on a dark InfoBox background.") },
  nullptr
};

std::unique_ptr<Widget>
CreateLayoutConfigPanel()
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  struct Fields {
    InfoBoxSettings::Geometry geometry;
    InfoBoxSettings::Geometry original;
    int title_scale;
    DialogSettings::TabStyle tab_style;
    UISettings::PopupMessagePosition popup_position;
    bool use_colors;
    InfoBoxSettings::Theme theme;
    InfoBoxSettings::BorderStyle border;
    bool show_menu;
    bool show_zoom;
    bool show_quickmenu;
    bool saved = false;
  };

  auto fields = std::make_shared<Fields>(Fields{
    ui_settings.info_boxes.geometry,
    ui_settings.info_boxes.geometry,
    static_cast<int>(ui_settings.info_boxes.scale_title_font),
    ui_settings.dialog.tab_style,
    ui_settings.popup_message_position,
    ui_settings.info_boxes.use_colors,
    ui_settings.info_boxes.theme,
    ui_settings.info_boxes.border_style,
    ui_settings.show_menu_button,
    ui_settings.show_zoom_button,
    ui_settings.show_quickmenu_button,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddEnum(_("InfoBox geometry"),
                _("A list of possible InfoBox layouts. Do some trials to find the best for your screen size."),
                info_box_geometry_list, fields->geometry);
  list->AddInteger(_("InfoBox title size"),
                   _("Zoom factor for InfoBox title and comment text"),
                   "%d %%", "%d", 50, 150, 5, fields->title_scale, true);
  list->AddEnum(_("Tab dialog style"), nullptr,
                tabdialog_style_list, fields->tab_style);
  list->AddEnum(_("Message display"), nullptr,
                popup_msg_position_list, fields->popup_position, true);

  if (HasColors())
    list->AddSwitch(_("Colored InfoBoxes"),
                    _("If true, certain InfoBoxes will have coloured text. For example, the active waypoint "
                      "InfoBox will be blue when the glider is above final glide."),
                    fields->use_colors, true);

  list->AddEnum(_("InfoBox theme"), nullptr,
                infobox_theme_list, fields->theme, true);
  list->AddEnum(_("InfoBox border"), nullptr,
                infobox_border_list, fields->border, true);
  list->AddSwitch(_("Show Menu button"), _("Show the Menu button"),
                  fields->show_menu, true);
  list->AddSwitch(_("Show Zoom button"), _("Show the Zoom button"),
                  fields->show_zoom, true);
  list->AddSwitch(C_("Setting", "Show QuickMenu button"),
                  _("Show the QuickMenu button"),
                  fields->show_quickmenu, true);

  /* Another page, such as InfoBox sets, reads the live geometry.
     Cancel restores the value from when the page was opened. */
  list->SetLeaveCallback([fields] {
    CommonInterface::SetUISettings().info_boxes.geometry =
      fields->geometry;
    return true;
  });
  list->SetUnprepareCallback([fields] {
    if (!fields->saved)
      CommonInterface::SetUISettings().info_boxes.geometry =
        fields->original;
  });

  list->SetSaveCallback([fields](bool &changed) {
    fields->saved = true;
    UISettings &ui_settings = CommonInterface::SetUISettings();
    auto &info_boxes = ui_settings.info_boxes;
    bool layout_changed = false;

    if (fields->geometry != fields->original) {
      info_boxes.geometry = fields->geometry;
      Profile::Set(ProfileKeys::InfoBoxGeometry,
                   static_cast<unsigned>(fields->geometry));
      changed = layout_changed = true;
    }

    if (ConfigPanel::CommitSetting(changed, info_boxes.scale_title_font,
          static_cast<unsigned>(fields->title_scale),
          ProfileKeys::InfoBoxTitleScale))
      layout_changed = true;

    ConfigPanel::CommitSetting(changed,
      ui_settings.popup_message_position, fields->popup_position,
      ProfileKeys::AppStatusMessageAlignment);

    if (HasColors())
      ConfigPanel::CommitSetting(changed, info_boxes.use_colors,
        fields->use_colors, ProfileKeys::AppInfoBoxColors);

    ConfigPanel::CommitSetting(changed, info_boxes.theme,
      fields->theme, ProfileKeys::AppInfoBoxTheme);
    ConfigPanel::CommitSetting(changed, info_boxes.border_style,
      fields->border, ProfileKeys::AppInfoBoxBorder);

    const bool menu_changed =
      ConfigPanel::CommitSetting(changed, ui_settings.show_menu_button,
        fields->show_menu, ProfileKeys::ShowMenuButton);
    const bool zoom_changed =
      ConfigPanel::CommitSetting(changed, ui_settings.show_zoom_button,
        fields->show_zoom, ProfileKeys::ShowZoomButton);
    const bool quickmenu_changed =
      ConfigPanel::CommitSetting(changed,
        ui_settings.show_quickmenu_button, fields->show_quickmenu,
        ProfileKeys::ShowQuickMenuButton);
    if (menu_changed || zoom_changed || quickmenu_changed)
      CommonInterface::main_window->ReinitialiseMapOverlayButtons();

    ConfigPanel::CommitSetting(changed, ui_settings.dialog.tab_style,
      fields->tab_style, ProfileKeys::AppDialogTabStyle);

    if (layout_changed)
      CommonInterface::main_window->ReinitialiseLayout();
    return true;
  });

  return list;
}
