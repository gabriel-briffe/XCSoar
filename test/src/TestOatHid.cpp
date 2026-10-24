// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Device/Driver/OatHid/Parser.hpp"
#include "TestUtil.hpp"

#include <array>

static std::array<std::byte, 4>
Report(unsigned b0, unsigned b1, unsigned b2, unsigned b3) noexcept
{
  return {
    std::byte{static_cast<unsigned char>(b0)},
    std::byte{static_cast<unsigned char>(b1)},
    std::byte{static_cast<unsigned char>(b2)},
    std::byte{static_cast<unsigned char>(b3)},
  };
}

int
main()
{
  plan_tests(11);

  /* 01 0E 06 00 -> 15.50 C */
  const auto warm = OatHid::Parse(Report(0x01, 0x0e, 0x06, 0x00));
  ok1(warm.recognized);
  ok1(warm.ok);
  ok1(warm.hundredths == 1550);

  /* -35.00 C, status ok */
  const auto cold = OatHid::Parse(Report(0x01, 0x54, 0xf2, 0x00));
  ok1(cold.recognized);
  ok1(cold.ok);
  ok1(cold.hundredths == -3500);

  /* status 1: the temperature bytes are not a reading */
  const auto fault = OatHid::Parse(Report(0x01, 0x0e, 0x06, 0x01));
  ok1(fault.recognized);
  ok1(!fault.ok);

  ok1(!OatHid::Parse(Report(0x02, 0x0e, 0x06, 0x00)).recognized);
  ok1(!OatHid::Parse(Report(0x01, 0x0e, 0x06, 0x02)).recognized);
  ok1(!OatHid::Parse(std::span<const std::byte>{}).recognized);

  return exit_status();
}
