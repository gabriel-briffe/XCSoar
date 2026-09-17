// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

namespace UI { class SingleWindow; }

/**
 * Full-screen Config menu: flat list of Config1/2/3 actions in a
 * Garmin-style tile grid (portrait 3×4, landscape 4×3) with bottom
 * page and Close buttons.
 */
void
dlgConfigMenuShowModal(UI::SingleWindow &parent) noexcept;

/**
 * Full-screen Tools submenu of the Config tile grid (Data Management,
 * Waypoint Editor, WeGlide Upload, Replay).
 */
void
dlgConfigToolsShowModal(UI::SingleWindow &parent) noexcept;
