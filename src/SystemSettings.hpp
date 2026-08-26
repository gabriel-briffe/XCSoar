// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Device/Config.hpp"
#include "Device/Features.hpp"

#include <type_traits>
#include <array>

struct SystemSettings {
  std::array<DeviceConfig, NUMDEV> devices;

  /**
   * When true, MergeThread rate limits are multiplied by 10 (saves
   * CPU/battery; higher latency for vario audio/bar).
   */
  bool slow_merge_thread;

  void SetDefaults();
};

static_assert(std::is_trivial<SystemSettings>::value, "type is not trivial");
