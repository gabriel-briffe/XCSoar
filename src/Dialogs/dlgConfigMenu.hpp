// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

namespace UI { class SingleWindow; }

/**
 * Full-screen tiled menu for Config: flat list of Config1/2/3
 * actions in a Garmin-style tile grid (portrait 3×4, landscape 4×3)
 * with bottom page and Close buttons.  Includes a per-menu Hidden
 * folder (long-press tiles to hide/unhide), plus Data, Display, Glide
 * Computer, and Task folders after Configuration.
 */
void
dlgConfigMenuShowModal(UI::SingleWindow &parent) noexcept;

/**
 * Full-screen Data submenu of the Config tile grid (site files,
 * download manager, export/import, backup, file explorer).
 */
void
dlgConfigDataShowModal(UI::SingleWindow &parent) noexcept;

/**
 * Full-screen Tools submenu of the Config tile grid (Waypoint Editor,
 * WeGlide Upload, Replay).  Shares the same tiled menu Hidden-folder
 * behaviour as Config.
 */
void
dlgConfigToolsShowModal(UI::SingleWindow &parent) noexcept;
