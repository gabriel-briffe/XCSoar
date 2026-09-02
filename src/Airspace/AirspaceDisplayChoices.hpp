// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Shared Airspace display choice lists (Config panel and page overrides).
 * Class colours stay outside the page-setting catalog; class filters use
 * #airspace_class_filter_mode_choices.
 */

#include "Form/DataField/Enum.hpp"
#include "Language/Language.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"

#include <cstdint>

/** Combined display + warning mode for one airspace class. */
enum class AirspaceClassFilterMode : uint8_t {
  NONE = 0,
  WARN = 1,
  DISPLAY = 2,
  WARN_AND_DISPLAY = 3,
};

static constexpr StaticEnumChoice airspace_display_mode_choices[] = {
  { AirspaceDisplayMode::ALLON, N_("All on"),
    N_("All airspaces are displayed.") },
  { AirspaceDisplayMode::CLIP, N_("Clip"),
    N_("Display airspaces below the clip altitude.") },
  { AirspaceDisplayMode::AUTO, NC_("Setting", "Auto"),
    N_("Display airspaces within a margin of the glider.") },
  { AirspaceDisplayMode::ALLBELOW, N_("All below"),
    N_("Display airspaces below the glider or within a margin.") },
  nullptr
};

static constexpr StaticEnumChoice airspace_label_visibility_choices[] = {
  { AirspaceRendererSettings::LabelSelection::NONE, N_("None"),
    N_("No labels will be displayed.") },
  { AirspaceRendererSettings::LabelSelection::ALL, N_("All"),
    N_("All labels will be displayed.") },
  nullptr
};

static constexpr StaticEnumChoice airspace_fill_mode_choices[] = {
  { AirspaceRendererSettings::FillMode::DEFAULT, N_("Default"),
    N_("This selects the best performing option for your hardware. "
       "In fact it favours 'fill padding' except for PPC 2000 system.") },
  { AirspaceRendererSettings::FillMode::ALL, N_("Fill all"),
    N_("Transparently fills the airspace colour over the whole area.") },
  { AirspaceRendererSettings::FillMode::PADDING, N_("Fill padding"),
    N_("Draws a solid outline with a half transparent border around the "
       "airspace.") },
  { AirspaceRendererSettings::FillMode::NONE, N_("No fill"),
    N_("Don't fill the airspace area.") },
  nullptr
};

static constexpr StaticEnumChoice airspace_class_filter_mode_choices[] = {
  { AirspaceClassFilterMode::NONE, N_("None"),
    N_("Do not display or warn for this airspace class.") },
  { AirspaceClassFilterMode::WARN, N_("Warn"),
    N_("Warn for this airspace class without drawing it on the map.") },
  { AirspaceClassFilterMode::DISPLAY, N_("Display"),
    N_("Draw this airspace class on the map without warnings.") },
  { AirspaceClassFilterMode::WARN_AND_DISPLAY, N_("Warn + Display"),
    N_("Draw this airspace class on the map and enable warnings.") },
  nullptr
};
