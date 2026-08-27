// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "ui/canvas/Color.hpp"

#ifdef XCSOAR_TESTING
static constexpr Color COLOR_XCSOAR_LIGHT = Color(0xed, 0x90, 0x90);
static constexpr Color COLOR_XCSOAR = Color(0xd0, 0x17, 0x17);
static constexpr Color COLOR_XCSOAR_DARK = Color(0x5d, 0x0a, 0x0a);
#else
static constexpr Color COLOR_XCSOAR_LIGHT = Color(0xaa, 0xc9, 0xe4);
static constexpr Color COLOR_XCSOAR = Color(0x3f, 0x76, 0xa8);
static constexpr Color COLOR_XCSOAR_DARK = Color(0x00, 0x31, 0x5e);
#endif

/**
 * Dark mode color palette derived from the XCSoar brand color.
 */
static constexpr Color COLOR_DARK_THEME_BACKGROUND =
  Color(0x0a, 0x15, 0x1f);
static constexpr Color COLOR_DARK_THEME_CAPTION =
  Color(0x10, 0x10, 0x10);
static constexpr Color COLOR_DARK_THEME_CAPTION_INACTIVE =
  Color(0x30, 0x30, 0x30);
static constexpr Color COLOR_DARK_THEME_LIST =
  Color(0x2a, 0x2a, 0x2a);
static constexpr Color COLOR_DARK_THEME_LIST_SELECTED =
  Color(0x3a, 0x3a, 0x3a);
static constexpr Color COLOR_DARK_THEME_BUTTON =
  Color(0x1e, 0x33, 0x48);
static constexpr Color COLOR_DARK_THEME_GRADIENT_TOP =
  Color(0x14, 0x22, 0x32);

/**
 * Light mode dialog background colors (warm parchment tint).
 */
static constexpr Color COLOR_DIALOG_BACKGROUND =
  Color(0xe2, 0xdc, 0xbe);
static constexpr Color COLOR_DIALOG_GRADIENT_TOP =
  Color(0xf0, 0xeb, 0xd4);

/**
 * Admonition colors for Markdown rendering.
 */
static constexpr Color COLOR_ADMONITION_IMPORTANT =
  Color(0xd0, 0x6b, 0x00);
static constexpr Color COLOR_ADMONITION_IMPORTANT_DARK =
  Color(0xff, 0xa0, 0x30);
static constexpr Color COLOR_ADMONITION_TIP =
  Color(0x00, 0x80, 0x00);

/**
 * A muted green readable on light backgrounds.
 * Standard COLOR_GREEN (0,255,0) is too bright on white.
 */
static constexpr Color COLOR_LIGHT_GREEN = Color(0x00, 0xc0, 0x00);

/**
 * Airspace warning list / map-item status badge colours.
 */
static constexpr Color COLOR_AIRSPACE_WARNING_INSIDE =
  Color(254, 50, 50);
static constexpr Color COLOR_AIRSPACE_WARNING_NEAR =
  Color(254, 254, 50);
static constexpr Color COLOR_AIRSPACE_WARNING_INSIDE_ACK =
  Color(254, 100, 100);
static constexpr Color COLOR_AIRSPACE_WARNING_NEAR_ACK =
  Color(254, 254, 100);

/**
 * XCTherm overlay palette.
 *
 * AROME-like ramps with HSL S=100% and even H/L steps; cream for
 * ±0.2; >+4 purple matches dark-red lightness (#8F008F).
 */
static constexpr Color COLOR_XCTHERM_BLUE = Color(0x00, 0x00, 0x8f);       /* ≤-3 */
static constexpr Color COLOR_XCTHERM_BRIGHT_CYAN = Color(0x00, 0x31, 0xb2); /* mid -2.5 */
static constexpr Color COLOR_XCTHERM_SKY_BLUE = Color(0x00, 0x76, 0xd6);    /* mid -1.5 */
static constexpr Color COLOR_XCTHERM_LIGHT_BLUE = Color(0x00, 0xca, 0xf5);  /* mid -0.75 */
static constexpr Color COLOR_XCTHERM_PALE_BLUE = Color(0x1a, 0xff, 0xe8);   /* mid -0.35 */
static constexpr Color COLOR_XCTHERM_CREAM = Color(0xe3, 0xe3, 0xa0);       /* ±0.2 */
static constexpr Color COLOR_XCTHERM_YELLOW = Color(0xff, 0xff, 0x00);      /* mid +0.35 */
static constexpr Color COLOR_XCTHERM_GOLD = Color(0xe3, 0xaa, 0x00);        /* mid +0.75 */
static constexpr Color COLOR_XCTHERM_ORANGE = Color(0xc7, 0x63, 0x00);      /* mid +1.5 */
static constexpr Color COLOR_XCTHERM_RED_ORANGE = Color(0xab, 0x2b, 0x00);  /* mid +2.5 */
static constexpr Color COLOR_XCTHERM_RED = Color(0x8f, 0x00, 0x00);         /* mid +3.5 */
static constexpr Color COLOR_XCTHERM_PURPLE = Color(0x8f, 0x00, 0x8f);      /* >+4 */


/** Instantaneous external wind arrow (map overlay). */
static constexpr Color COLOR_WIND_ARROW_INSTANTANEOUS =
  Color(0x80, 0x80, 0xff);

static constexpr uint8_t ALPHA_OVERLAY = 0xA0;
