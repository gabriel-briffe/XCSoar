// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * Vendor HID report from the OAT probe (USB 2E8A:0A01).
 *
 * Four bytes: report id 1, int16 little-endian hundredths of a degree
 * Celsius, then a status byte.  Status 0 is a reading.  Status 1 is a
 * sensor fault; the temperature bytes are then not a reading.
 */
namespace OatHid {

struct Report {
  /** The four bytes are a report this probe sends. */
  bool recognized = false;

  /** Status is 0, so #hundredths is a temperature. */
  bool ok = false;

  int16_t hundredths = 0;
};

[[nodiscard]]
Report
Parse(std::span<const std::byte> bytes) noexcept;

} // namespace OatHid
