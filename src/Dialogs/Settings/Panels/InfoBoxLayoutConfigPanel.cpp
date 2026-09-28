// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InfoBoxLayoutConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Asset.hpp"
#include "Form/DataField/Enum.hpp"
#include "InfoBoxes/InfoBoxGeometryList.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MainWindow.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"

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

/**
 * Where the InfoBoxes sit, and how their titles and borders look.
 * Leaving the page publishes the geometry so another page can read
 * it; closing without saving puts the original back.
 */
class InfoBoxLayoutConfigPanel final : public ConfigListPanel {
  InfoBoxSettings::Geometry geometry;
  InfoBoxSettings::Geometry original;
  int title_scale;
  bool use_colors;
  InfoBoxSettings::Theme theme;
  InfoBoxSettings::BorderStyle border;
  bool saved = false;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  void Unprepare() noexcept override;
  bool Save(bool &changed) noexcept override;
  bool Leave() noexcept override;
};

void
InfoBoxLayoutConfigPanel::LoadSettings() noexcept
{
  const auto &info_boxes = CommonInterface::GetUISettings().info_boxes;

  geometry = info_boxes.geometry;
  original = info_boxes.geometry;
  title_scale = static_cast<int>(info_boxes.scale_title_font);
  use_colors = info_boxes.use_colors;
  theme = info_boxes.theme;
  border = info_boxes.border_style;
}

void
InfoBoxLayoutConfigPanel::Fill() noexcept
{
  AddGroup();

  AddEnumItem(_("InfoBox geometry"),
              _("A list of possible InfoBox layouts. Do some trials to find "
                "the best for your screen size."),
              info_box_geometry_list, geometry);

  if (IsExpert())
    AddPercentItem(_("InfoBox title size"),
                   _("Zoom factor for InfoBox title and comment text"),
                   50, 150, 5, title_scale);

  if (HasColors() && IsExpert())
    AddToggleItem(_("Colored InfoBoxes"),
                  _("If true, certain InfoBoxes will have coloured text. "
                    "For example, the active waypoint InfoBox will be blue "
                    "when the glider is above final glide."),
                  use_colors);

  if (IsExpert()) {
    AddEnumItem(_("InfoBox theme"), nullptr,
                infobox_theme_list, theme);
    AddEnumItem(_("InfoBox border"), nullptr,
                infobox_border_list, border);
  }
}

void
InfoBoxLayoutConfigPanel::Unprepare() noexcept
{
  if (!saved)
    CommonInterface::SetUISettings().info_boxes.geometry = original;

  ConfigListPanel::Unprepare();
}

bool
InfoBoxLayoutConfigPanel::Save(bool &_changed) noexcept
{
  saved = true;

  bool changed = false;
  auto &info_boxes = CommonInterface::SetUISettings().info_boxes;
  bool layout_changed = false;

  if (geometry != original) {
    info_boxes.geometry = geometry;
    Profile::Set(ProfileKeys::InfoBoxGeometry,
                 static_cast<unsigned>(geometry));
    changed = layout_changed = true;
  }

  if (Profile::Update(ProfileKeys::InfoBoxTitleScale,
                      info_boxes.scale_title_font,
                      static_cast<unsigned>(title_scale))) {
    changed = true;
    layout_changed = true;
  }

  if (HasColors())
    changed |= Profile::Update(ProfileKeys::AppInfoBoxColors,
                               info_boxes.use_colors, use_colors);

  changed |= Profile::Update(ProfileKeys::AppInfoBoxTheme,
                             info_boxes.theme, theme);
  changed |= Profile::Update(ProfileKeys::AppInfoBoxBorder,
                             info_boxes.border_style, border);

  if (layout_changed)
    CommonInterface::main_window->ReinitialiseLayout();

  _changed |= changed;
  return true;
}

bool
InfoBoxLayoutConfigPanel::Leave() noexcept
{
  /* Another page, such as InfoBox sets, reads the live geometry. */
  CommonInterface::SetUISettings().info_boxes.geometry = geometry;
  return ConfigListPanel::Leave();
}

std::unique_ptr<Widget>
CreateInfoBoxLayoutConfigPanel()
{
  return std::make_unique<InfoBoxLayoutConfigPanel>();
}
