// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InfoBoxLayoutConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "Interface.hpp"
#include "MainWindow.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "Form/DataField/Enum.hpp"
#include "Asset.hpp"

enum ControlIndex {
  AppInfoBoxGeom,
  InfoBoxTitleScale,
  AppInfoBoxColors,
  AppInfoBoxTheme,
  AppInfoBoxBorder,
};

static constexpr StaticEnumChoice info_box_geometry_list[] = {
  { InfoBoxSettings::Geometry::SPLIT_8,
    N_("8 Split") },
  { InfoBoxSettings::Geometry::SPLIT_10,
    N_("10 Split") },
  { InfoBoxSettings::Geometry::SPLIT_3X4,
    N_("12 Split in 3 rows") },
  { InfoBoxSettings::Geometry::SPLIT_3X5,
    N_("15 Split in 3 rows") },
  { InfoBoxSettings::Geometry::SPLIT_3X6,
    N_("18 Split in 3 rows") },
  { InfoBoxSettings::Geometry::BOTTOM_RIGHT_8,
    N_("8 Bottom or Right") },
  { InfoBoxSettings::Geometry::BOTTOM_8_VARIO,
    N_("8 Bottom + Vario (Portrait)") },
  { InfoBoxSettings::Geometry::TOP_LEFT_8,
    N_("8 Top or Left") },
  { InfoBoxSettings::Geometry::TOP_8_VARIO,
    N_("8 Top + Vario (Portrait)") },
  { InfoBoxSettings::Geometry::RIGHT_9_VARIO,
    N_("9 Right + Vario (Landscape)") },
  { InfoBoxSettings::Geometry::LEFT_6_RIGHT_3_VARIO,
    N_("9 Left + Right + Vario (Landscape)") },
  { InfoBoxSettings::Geometry::LEFT_12_RIGHT_3_VARIO,
    N_("12 Left + 3 Right Vario (Landscape)") },
  { InfoBoxSettings::Geometry::RIGHT_5,
    N_("5 Right (Square)") },
  { InfoBoxSettings::Geometry::BOTTOM_RIGHT_10,
    N_("10 Bottom or Right") },
  { InfoBoxSettings::Geometry::BOTTOM_RIGHT_12,
    N_("12 Bottom or Right") },
  { InfoBoxSettings::Geometry::TOP_LEFT_10,
    N_("10 Top or Left") },
  { InfoBoxSettings::Geometry::TOP_LEFT_12,
    N_("12 Top or Left") },
  { InfoBoxSettings::Geometry::RIGHT_16,
    N_("16 Right (Landscape)") },
  { InfoBoxSettings::Geometry::RIGHT_24,
    N_("24 Bottom or Right") },
  { InfoBoxSettings::Geometry::TOP_LEFT_4,
    N_("4 Top or Left") },
  { InfoBoxSettings::Geometry::BOTTOM_RIGHT_4,
    N_("4 Bottom or Right") },
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

class InfoBoxLayoutConfigPanel final : public RowFormWidget {
  /** Geometry when this panel was opened; restored if Settings is cancelled. */
  InfoBoxSettings::Geometry original_geometry{};
  bool saved = false;

public:
  InfoBoxLayoutConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  void Unprepare() noexcept override;
  bool Leave() noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
InfoBoxLayoutConfigPanel::Prepare(ContainerWindow &parent,
                                   const PixelRect &rc) noexcept
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  original_geometry = ui_settings.info_boxes.geometry;
  saved = false;

  RowFormWidget::Prepare(parent, rc);

  AddEnum(_("InfoBox geometry"),
          _("A list of possible InfoBox layouts. Do some trials to find the best for your screen size."),
          info_box_geometry_list, (unsigned)ui_settings.info_boxes.geometry);

  AddInteger(_("InfoBox title size"), _("Zoom factor for InfoBox title and comment text"),
             "%d %%", "%d", 50, 150, 5,
             ui_settings.info_boxes.scale_title_font);
  SetExpertRow(InfoBoxTitleScale);

  if (HasColors()) {
    AddBoolean(_("Colored InfoBoxes"),
               _("If true, certain InfoBoxes will have coloured text. For example, the active waypoint "
                 "InfoBox will be blue when the glider is above final glide."),
               ui_settings.info_boxes.use_colors);
    SetExpertRow(AppInfoBoxColors);
  } else
    AddDummy();

  AddEnum(_("InfoBox theme"), nullptr, infobox_theme_list,
          (unsigned)ui_settings.info_boxes.theme);
  SetExpertRow(AppInfoBoxTheme);

  AddEnum(_("InfoBox border"), nullptr, infobox_border_list,
          unsigned(ui_settings.info_boxes.border_style));
  SetExpertRow(AppInfoBoxBorder);
}

void
InfoBoxLayoutConfigPanel::Unprepare() noexcept
{
  if (!saved)
    CommonInterface::SetUISettings().info_boxes.geometry = original_geometry;

  RowFormWidget::Unprepare();
}

bool
InfoBoxLayoutConfigPanel::Leave() noexcept
{
  /* Switching to another settings page (still inside Configuration):
     copy geometry so InfoBox Sets can read settings.geometry. */
  SaveValueEnum(AppInfoBoxGeom,
                CommonInterface::SetUISettings().info_boxes.geometry);
  return true;
}

bool
InfoBoxLayoutConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;

  UISettings &ui_settings = CommonInterface::SetUISettings();
  saved = true;

  bool info_box_geometry_changed = false;

  /* Leave() may already have synced the DataField into ui_settings;
     re-base so SaveValueEnum still writes the profile when needed. */
  ui_settings.info_boxes.geometry = original_geometry;
  info_box_geometry_changed |=
    SaveValueEnum(AppInfoBoxGeom, ProfileKeys::InfoBoxGeometry,
                  ui_settings.info_boxes.geometry);
  info_box_geometry_changed |=
    SaveValueInteger(InfoBoxTitleScale, ProfileKeys::InfoBoxTitleScale,
                  ui_settings.info_boxes.scale_title_font);

  changed |= info_box_geometry_changed;

  if (HasColors())
    changed |= SaveValue(AppInfoBoxColors, ProfileKeys::AppInfoBoxColors,
                         ui_settings.info_boxes.use_colors);

  changed |= SaveValueEnum(AppInfoBoxTheme, ProfileKeys::AppInfoBoxTheme,
                           ui_settings.info_boxes.theme);

  changed |= SaveValueEnum(AppInfoBoxBorder, ProfileKeys::AppInfoBoxBorder,
                           ui_settings.info_boxes.border_style);

  if (info_box_geometry_changed)
    CommonInterface::main_window->ReinitialiseLayout();

  _changed |= changed;

  return true;
}

std::unique_ptr<Widget>
CreateInfoBoxLayoutConfigPanel()
{
  return std::make_unique<InfoBoxLayoutConfigPanel>();
}
