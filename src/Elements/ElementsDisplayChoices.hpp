// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Shared Map Display → Elements choice lists (Config panel and page
 * overrides).
 */

#include "Form/DataField/Enum.hpp"
#include "Language/Language.hpp"
#include "MapSettings.hpp"

static constexpr StaticEnumChoice ground_track_mode_choices[] = {
  { DisplayGroundTrack::OFF, N_("Off"),
    N_("Disable display of ground track line.") },
  { DisplayGroundTrack::ON, N_("On"),
    N_("Always display ground track line.") },
  { DisplayGroundTrack::AUTO, NC_("Setting", "Auto"),
    N_("Display ground track line if there is a significant difference to "
       "plane heading.") },
  nullptr
};

static constexpr StaticEnumChoice trail_length_choices[] = {
  { TrailSettings::Length::OFF, N_("Off") },
  { TrailSettings::Length::LONG, N_("Long") },
  { TrailSettings::Length::SHORT, N_("Short") },
  { TrailSettings::Length::FULL, N_("Full") },
  nullptr
};

static constexpr StaticEnumChoice trail_type_choices[] = {
  { TrailSettings::Type::VARIO_1, N_("Vario #1"),
    N_("Within lift areas lines get displayed green and thicker, while "
       "sinking lines are shown brown and thin. Zero lift is presented as a "
       "grey line.") },
  { TrailSettings::Type::VARIO_1_DOTS, N_("Vario #1 (with dots)"),
    N_("The same colour scheme as the previous, but with dotted lines while "
       "sinking.") },
  { TrailSettings::Type::VARIO_2, N_("Vario #2"),
    N_("The climb colour for this scheme is orange to red, sinking is "
       "displayed as light blue to dark blue. Zero lift is presented as a "
       "yellow line.") },
  { TrailSettings::Type::VARIO_2_DOTS, N_("Vario #2 (with dots)"),
    N_("The same colour scheme as the previous, but with dotted lines while "
       "sinking.") },
  { TrailSettings::Type::VARIO_DOTS_AND_LINES,
    N_("Vario-scaled dots and lines"),
    N_("Vario-scaled dots with lines. Orange to red = climb. Light blue to "
       "dark blue = sink. Zero lift is presented as a yellow line.") },
  { TrailSettings::Type::VARIO_EINK, N_("Vario E-ink"),
    N_("E-ink friendly color scheme, lighter and thicker dots means lift "
       "while darker and thinner means sink.") },
  { TrailSettings::Type::ALTITUDE, N_("Altitude"),
    N_("The colour scheme corresponds to the height.") },
  nullptr
};

static constexpr StaticEnumChoice aircraft_symbol_choices[] = {
  { AircraftSymbol::SIMPLE, N_("Simple"),
    N_("Simplified line graphics, black with white contours.") },
  { AircraftSymbol::SIMPLE_LARGE, N_("Simple (large)"),
    N_("Enlarged simple graphics.") },
  { AircraftSymbol::DETAILED, N_("Detailed"),
    N_("Detailed rendered aircraft graphics.") },
  { AircraftSymbol::HANGGLIDER, N_("HangGlider"),
    N_("Simplified hang glider as line graphics, white with black contours.") },
  { AircraftSymbol::PARAGLIDER, N_("Paraglider"),
    N_("Simplified para glider as line graphics, white with black contours.") },
  nullptr
};

static constexpr StaticEnumChoice wind_arrow_choices[] = {
  { WindArrowStyle::NO_ARROW, N_("Off"), N_("No wind arrow is drawn.") },
  { WindArrowStyle::ARROW_HEAD, N_("Arrow head"),
    N_("Draws an arrow head only.") },
  { WindArrowStyle::FULL_ARROW, N_("Full arrow"),
    N_("Draws an arrow head with a dashed arrow line.") },
  nullptr
};

static constexpr StaticEnumChoice online_traffic_map_mode_choices[] = {
  { DisplayOnlineTrafficMapMode::OFF, N_("Off"),
    N_("No online traffic is drawn.") },
  { DisplayOnlineTrafficMapMode::SYMBOL, N_("Symbol"),
    N_("Draws the traffic symbol only.") },
  { DisplayOnlineTrafficMapMode::SYMBOL_NAME, N_("Symbol and Name"),
    N_("Draws the traffic symbol with name.") },
  nullptr
};
