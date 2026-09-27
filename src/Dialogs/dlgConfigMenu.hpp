// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

namespace UI { class SingleWindow; }

class Menu;

/**
 * Full-screen tiled Config menu (portrait 3×4 / landscape 4×3 grid).
 * Folder panels come from ConfigMenuData (shared with the list
 * Configuration dialog); softkey actions still come from XCI
 * Config1/2/3.  Includes per-menu Hidden tiles and nested Data,
 * Flight Display, Glide Computer, Task, Tools, and System Setup.
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

/**
 * Show a full-screen Garmin-style tiled menu built from an InputEvents
 * #Menu (same chrome as Config: pages, Close, Hidden folder).
 */
void
ShowTiledMenuFromMenu(UI::SingleWindow &parent,
                      const char *title,
                      const char *menu_id,
                      const Menu &menu) noexcept;

/**
 * Ask nested tiled menus to dismiss all the way back to the map.
 * Used by settings-panel Close when opened from a tiled menu.
 */
void
RequestTiledMenuCloseAll() noexcept;
