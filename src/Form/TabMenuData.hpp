// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include <memory>

class Widget;

struct TabMenuPage {
  const char *menu_caption;

  std::unique_ptr<Widget> (*Load)();

  /**
   * Optional gate: when non-null, the page is omitted unless this
   * returns true (e.g. Glide Cone needs GLES 3.1 compute).
   */
  bool (*available)() noexcept = nullptr;
};

struct TabMenuGroup {
  const char *caption;

  const TabMenuPage *pages;
};
