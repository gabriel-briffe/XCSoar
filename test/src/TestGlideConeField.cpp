// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideCone/GlideConeField.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"
#include "Math/Angle.hpp"
#include "TestUtil.hpp"

static GlideConeField
MakeStrip() noexcept
{
  GlideConeField f;
  constexpr unsigned n = 5;
  f.result.width = n;
  f.result.height = 1;
  f.result.altitudes.assign(n, 9000.f);
  f.result.origin_x.assign(n, -1);
  f.result.origin_y.assign(n, 0);
  f.result.ground.assign(n, 0);
  f.elevation.assign(n, 0.f);
  f.cell_size_m = 400;
  f.cell_size_x_m = 400;
  f.cell_size_y_m = 400;
  f.glide_ratio = 20;
  f.max_alt = 9000;
  f.bounds = GeoBounds(GeoPoint(Angle::Degrees(0), Angle::Degrees(1)),
                       GeoPoint(Angle::Degrees(5), Angle::Degrees(0)));
  f.home_x = 0;
  f.home_y = 0;
  f.seeds.push_back({0, 0, 100.f});
  return f;
}

static GeoPoint
CellCentre(const GlideConeField &f, int x, int y) noexcept
{
  return f.CellToGeo(x, y);
}

int
main()
{
  plan_tests(14);

  GlideConeField air = MakeStrip();
  /* seed (air) <- air <- ground <- ground <- ground (aircraft) */
  air.result.altitudes[0] = 100;
  air.result.origin_x[0] = 0;
  air.result.altitudes[1] = 120;
  air.result.origin_x[1] = 0;
  air.result.altitudes[2] = 500;
  air.result.origin_x[2] = 1;
  air.result.ground[2] = 1;
  air.result.altitudes[3] = 500;
  air.result.origin_x[3] = 2;
  air.result.ground[3] = 1;
  air.result.altitudes[4] = 500;
  air.result.origin_x[4] = 3;
  air.result.ground[4] = 1;
  air.elevation = {100, 90, 80, 70, 60};

  ok1(air.IsValid());

  int gx = -1, gy = -1;
  ok1(air.GeoToCell(CellCentre(air, 4, 0), gx, gy));
  ok1(gx == 4 && gy == 0);

  const auto air_req = air.RequiredAltitude(CellCentre(air, 1, 0));
  ok1(air_req && equals(*air_req, 120));

  /* 3 hops of 400 m / L/D 20 = 60 m extra on the first air cell */
  const auto gnd_req = air.RequiredAltitude(CellCentre(air, 4, 0));
  ok1(gnd_req && equals(*gnd_req, 180));

  const auto near_req = air.RequiredAltitude(CellCentre(air, 2, 0));
  ok1(near_req && equals(*near_req, 140));

  const auto path = air.Trace(CellCentre(air, 4, 0));
  ok1(path.size() == 5);
  ok1(path.front().x == 4 && path.back().x == 0);

  ok1(air.IsDownhillGroundSegment(2, 0, 3, 0));
  ok1(!air.IsDownhillGroundSegment(1, 0, 0, 0)); /* from-cell is air */
  ok1(!air.IsDownhillGroundSegment(2, 0, 1, 0)); /* terrain rises */

  GlideConeField all_gnd = MakeStrip();
  all_gnd.seeds.front().alt = 80;
  for (unsigned i = 0; i < 5; ++i) {
    all_gnd.result.altitudes[i] = 400;
    all_gnd.result.origin_x[i] = i == 0 ? 0 : int(i) - 1;
    all_gnd.result.ground[i] = 1;
    all_gnd.elevation[i] = 400;
  }
  /* 4 hops of 400 m / 20 = 80 m plus seed arrival 80 */
  const auto seed_req = all_gnd.RequiredAltitude(CellCentre(all_gnd, 4, 0));
  ok1(seed_req && equals(*seed_req, 160));

  air.result.altitudes[4] = air.max_alt;
  ok1(!air.RequiredAltitude(CellCentre(air, 4, 0)));

  GeoPoint outside(Angle::Degrees(90), Angle::Degrees(90));
  ok1(!air.RequiredAltitude(outside));

  return exit_status();
}
