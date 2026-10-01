// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Terrain/DemOverview.hpp"
#include "TestUtil.hpp"

#include <array>

static void
TestSelect() noexcept
{
  ok1(DemOverview::Select(0) == DemOverview::Lod::FINE);
  ok1(DemOverview::Select(1) == DemOverview::Lod::FINE);
  ok1(DemOverview::Select(3.9) == DemOverview::Lod::FINE);
  ok1(DemOverview::Select(4) == DemOverview::Lod::MEDIUM);
  ok1(DemOverview::Select(8) == DemOverview::Lod::MEDIUM);
  ok1(DemOverview::Select(15.9) == DemOverview::Lod::MEDIUM);
  ok1(DemOverview::Select(16) == DemOverview::Lod::COARSE);
  ok1(DemOverview::Select(100) == DemOverview::Lod::COARSE);

  ok1(DemOverview::Bits(DemOverview::Lod::FINE) == 0);
  ok1(DemOverview::Bits(DemOverview::Lod::MEDIUM) == 2);
  ok1(DemOverview::Bits(DemOverview::Lod::COARSE) == 4);

  /* Both manual: ladder by scale bar (cpp ignored). */
  ok1(DemOverview::Select(100, 100e3, 200e3, 600e3) ==
      DemOverview::Lod::FINE);
  ok1(DemOverview::Select(100, 200e3, 200e3, 600e3) ==
      DemOverview::Lod::MEDIUM);
  ok1(DemOverview::Select(100, 600e3, 200e3, 600e3) ==
      DemOverview::Lod::COARSE);

  /* Both Auto: cells-per-pixel. */
  ok1(DemOverview::Select(8, 50e3, 0, 0) == DemOverview::Lod::MEDIUM);
}

static void
TestMaxPool() noexcept
{
  /* 4×4 block: peak 100 in the corner must survive 4× max-pool. */
  std::array<TerrainHeight, 16> src{};
  for (auto &h : src)
    h = TerrainHeight(10);
  src[15] = TerrainHeight(100);

  ok1(DemOverview::MaxPool(src.data(), 4, 4, 0, 0, 4).GetValue() == 100);

  std::array<TerrainHeight, 1> dest{};
  DemOverview::Build(src.data(), 4, 4, dest.data(), 2);
  ok1(dest[0].GetValue() == 100);

  /* Water counts as 0; ground wins. */
  for (auto &h : src)
    h = TerrainHeight(-31000); /* water */
  src[0] = TerrainHeight(5);
  ok1(DemOverview::MaxPool(src.data(), 4, 4, 0, 0, 4).GetValue() == 5);

  /* All invalid → Invalid. */
  for (auto &h : src)
    h = TerrainHeight::Invalid();
  ok1(DemOverview::MaxPool(src.data(), 4, 4, 0, 0, 4).IsInvalid());

  /* 8×8 → 2×2 at bits=2: each cell is max of its 4×4. */
  std::array<TerrainHeight, 64> big{};
  for (auto &h : big)
    h = TerrainHeight(1);
  big[0] = TerrainHeight(20);
  big[4 + 4 * 8] = TerrainHeight(30);   /* (4,4) → overview (1,1) */
  big[7 + 7 * 8] = TerrainHeight(40);   /* same overview (1,1) */
  std::array<TerrainHeight, 4> mid{};
  DemOverview::Build(big.data(), 8, 8, mid.data(), 2);
  ok1(mid[0].GetValue() == 20);
  ok1(mid[3].GetValue() == 40);

  /* bits=3 (8×) collapses to one cell with global max. */
  std::array<TerrainHeight, 1> coarse{};
  DemOverview::Build(big.data(), 8, 8, coarse.data(), 3);
  ok1(coarse[0].GetValue() == 40);
}

int main()
{
  plan_tests(11 + 4 + 7);
  TestSelect();
  TestMaxPool();
  return exit_status();
}
