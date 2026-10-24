// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Parser.hpp"

namespace OatHid {

static constexpr uint8_t REPORT_ID = 1;
static constexpr uint8_t STATUS_OK = 0;
static constexpr uint8_t STATUS_FAULT = 1;
static constexpr std::size_t REPORT_SIZE = 4;

Report
Parse(std::span<const std::byte> bytes) noexcept
{
  Report report;
  if (bytes.size() != REPORT_SIZE)
    return report;

  if (static_cast<uint8_t>(bytes[0]) != REPORT_ID)
    return report;

  const uint8_t status = static_cast<uint8_t>(bytes[3]);
  if (status != STATUS_OK && status != STATUS_FAULT)
    return report;

  const unsigned lo = static_cast<uint8_t>(bytes[1]);
  const unsigned hi = static_cast<uint8_t>(bytes[2]);
  report.recognized = true;
  report.ok = status == STATUS_OK;
  report.hundredths = static_cast<int16_t>(lo | (hi << 8));
  return report;
}

} // namespace OatHid
