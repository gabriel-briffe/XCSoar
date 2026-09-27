// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Form/TabMenuData.hpp"

/**
 * Single source of truth for Configuration menu panels.
 *
 * Both the list Configuration dialog (TabMenuDisplay) and the tiled
 * Config folders consume these tables so the two UIs stay aligned.
 * The list groups mirror the tiled leaf folders (Flight Display,
 * Glide Computer, Task Defaults, System Setup, …).
 */
namespace ConfigMenuData {

extern const TabMenuPage map_pages[];
extern const TabMenuPage aircrafts_pages[];
extern const TabMenuPage gauge_pages[];
extern const TabMenuPage infoboxes_pages[];
extern const TabMenuPage pages_pages[];
extern const TabMenuPage computer_pages[];
extern const TabMenuPage task_defaults_pages[];
extern const TabMenuPage language_pages[];
extern const TabMenuPage hardware_pages[];
extern const TabMenuPage look_accessibility_pages[];
extern const TabMenuPage units_time_pages[];
extern const TabMenuPage setup_pages[];
extern const TabMenuPage weather_pages[];
extern const TabMenuPage accounts_pages[];
extern const TabMenuPage files_pages[];

/** Groups for dlgConfigurationShowModal (list UI). */
extern const TabMenuGroup list_groups[];
extern const unsigned list_group_count;

} // namespace ConfigMenuData
