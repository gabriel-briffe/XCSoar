// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "XCThermGeoJSONCleanup.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace XCThermGeoJSON {

namespace {

struct XY {
  double x, y;
};

/** Near-zero area in lon/lat degrees² (degenerate / collapsed). */
constexpr double AREA_EPS = 1e-18;

/** Vertex equality in lon/lat degrees. */
constexpr double VERTEX_EPS = 1e-12;

[[gnu::const]]
static bool
Near(XY a, XY b) noexcept
{
  return std::fabs(a.x - b.x) <= VERTEX_EPS &&
    std::fabs(a.y - b.y) <= VERTEX_EPS;
}

[[gnu::pure]]
static XY
ToXY(const GeoPoint &p) noexcept
{
  return {p.longitude.Degrees(), p.latitude.Degrees()};
}

[[gnu::pure]]
static GeoPoint
ToGeo(XY p) noexcept
{
  return GeoPoint(Angle::Degrees(p.x), Angle::Degrees(p.y));
}

static unsigned
StripClosingDuplicate(std::vector<XY> &pts) noexcept
{
  if (pts.size() >= 2 && Near(pts.front(), pts.back())) {
    pts.pop_back();
    return 1;
  }
  return 0;
}

static unsigned
RemoveConsecutiveDuplicates(std::vector<XY> &pts) noexcept
{
  if (pts.empty())
    return 0;

  const std::size_t before = pts.size();
  std::size_t w = 1;
  for (std::size_t r = 1; r < pts.size(); ++r) {
    if (!Near(pts[r], pts[w - 1]))
      pts[w++] = pts[r];
  }
  pts.resize(w);
  return unsigned(before - w);
}

[[gnu::pure]]
static double
SignedShoelace(const std::vector<XY> &pts) noexcept
{
  const std::size_t n = pts.size();
  if (n < 3)
    return 0;

  double sum = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const XY a = pts[i];
    const XY b = pts[(i + 1) % n];
    sum += a.x * b.y - b.x * a.y;
  }
  return 0.5 * sum;
}

[[gnu::pure]]
static double
AbsShoelace(const std::vector<XY> &pts) noexcept
{
  return std::fabs(SignedShoelace(pts));
}

/**
 * Proper intersection of open segments AB and CD (shared endpoints do
 * not count). Returns the intersection point when segments cross.
 */
[[gnu::pure]]
static std::optional<XY>
ProperIntersection(XY a, XY b, XY c, XY d) noexcept
{
  const double rx = b.x - a.x;
  const double ry = b.y - a.y;
  const double sx = d.x - c.x;
  const double sy = d.y - c.y;
  const double den = rx * sy - ry * sx;
  if (std::fabs(den) <= VERTEX_EPS)
    return std::nullopt; /* parallel / collinear */

  const double qx = c.x - a.x;
  const double qy = c.y - a.y;
  const double t = (qx * sy - qy * sx) / den;
  const double u = (qx * ry - qy * rx) / den;

  /* Strictly between endpoints — excludes shared vertices. */
  constexpr double lo = 1e-9;
  constexpr double hi = 1.0 - 1e-9;
  if (t <= lo || t >= hi || u <= lo || u >= hi)
    return std::nullopt;

  return XY{a.x + t * rx, a.y + t * ry};
}

struct Crossing {
  std::size_t i; /* edge pts[i] → pts[i+1] */
  std::size_t j; /* edge pts[j] → pts[j+1] */
  XY point;
};

[[gnu::pure]]
static std::optional<Crossing>
FindFirstCrossing(const std::vector<XY> &pts) noexcept
{
  const std::size_t n = pts.size();
  if (n < 4)
    return std::nullopt;

  for (std::size_t i = 0; i < n; ++i) {
    const XY a = pts[i];
    const XY b = pts[(i + 1) % n];
    for (std::size_t j = i + 1; j < n; ++j) {
      /* Skip adjacent edges and the wrap-around adjacent pair. */
      if (j == i || j == (i + 1) % n || i == (j + 1) % n)
        continue;
      if (i == 0 && j == n - 1)
        continue;

      if (auto hit = ProperIntersection(a, b, pts[j],
                                        pts[(j + 1) % n])) {
        Crossing c;
        c.i = i;
        c.j = j;
        c.point = *hit;
        return c;
      }
    }
  }

  return std::nullopt;
}

/**
 * Split a ring at one proper edge crossing into two open rings
 * (no repeated closing vertex).
 */
static std::pair<std::vector<XY>, std::vector<XY>>
SplitAtCrossing(const std::vector<XY> &pts, Crossing c) noexcept
{
  const std::size_t n = pts.size();
  std::vector<XY> a, b;
  a.reserve(n);
  b.reserve(n);

  a.push_back(c.point);
  for (std::size_t k = (c.i + 1) % n; k != (c.j + 1) % n;
       k = (k + 1) % n)
    a.push_back(pts[k]);

  b.push_back(c.point);
  for (std::size_t k = (c.j + 1) % n; k != (c.i + 1) % n;
       k = (k + 1) % n)
    b.push_back(pts[k]);

  return {std::move(a), std::move(b)};
}

static Ring
ToClosedRing(const std::vector<XY> &pts) noexcept
{
  Ring ring;
  ring.reserve(pts.size() + 1);
  for (const XY p : pts)
    ring.push_back(ToGeo(p));
  if (!ring.empty())
    ring.push_back(ring.front());
  return ring;
}

static void
AddStat(CleanupStats *stats, unsigned CleanupStats::*field,
        unsigned n) noexcept
{
  if (stats != nullptr && n > 0)
    stats->*field += n;
}

static void
AppendCleaned(std::vector<XY> pts,
              std::vector<std::vector<Ring>> &out,
              CleanupStats *stats,
              unsigned depth) noexcept
{
  /* Pathological rings could recurse; bail out rather than blow the
     stack — the leftover geometry is dropped as junk. */
  if (depth > 32) {
    AddStat(stats, &CleanupStats::junk, 1);
    return;
  }

  AddStat(stats, &CleanupStats::dedupe,
          RemoveConsecutiveDuplicates(pts));
  AddStat(stats, &CleanupStats::dedupe, StripClosingDuplicate(pts));

  if (pts.size() < 3) {
    AddStat(stats, &CleanupStats::junk, 1);
    return;
  }

  /* Self-crossing rings often have near-zero *signed* area (lobes
     cancel).  Split before the area junk check. */
  if (auto cross = FindFirstCrossing(pts)) {
    AddStat(stats, &CleanupStats::self_crossings, 1);
    auto [left, right] = SplitAtCrossing(pts, *cross);
    AppendCleaned(std::move(left), out, stats, depth + 1);
    AppendCleaned(std::move(right), out, stats, depth + 1);
    return;
  }

  if (AbsShoelace(pts) <= AREA_EPS) {
    AddStat(stats, &CleanupStats::junk, 1);
    return;
  }

  /* Keep simple rings intact (including concave); draw-time earcut
     fills them (with holes when present). */
  out.push_back({ToClosedRing(pts)});
}

} // namespace

std::vector<std::vector<Ring>>
CleanExterior(const Ring &exterior, CleanupStats *stats) noexcept
{
  std::vector<XY> pts;
  pts.reserve(exterior.size());
  for (const GeoPoint &p : exterior)
    pts.push_back(ToXY(p));

  std::vector<std::vector<Ring>> out;
  AppendCleaned(std::move(pts), out, stats, 0);
  return out;
}

void
CleanBandPolygons(WindBand &band, CleanupStats *stats) noexcept
{
  std::vector<std::vector<Ring>> cleaned;
  cleaned.reserve(band.polygons.size());

  for (const auto &polygon : band.polygons) {
    if (polygon.empty())
      continue;

    auto exteriors = CleanExterior(polygon[0], stats);
    if (exteriors.empty())
      continue;

    std::vector<Ring> holes;
    for (std::size_t i = 1; i < polygon.size(); ++i) {
      auto hole_parts = CleanExterior(polygon[i], stats);
      for (auto &part : hole_parts) {
        if (!part.empty())
          holes.push_back(std::move(part[0]));
      }
    }

    if (exteriors.size() == 1) {
      auto poly = std::move(exteriors[0]);
      for (auto &hole : holes)
        poly.push_back(std::move(hole));
      cleaned.push_back(std::move(poly));
    } else {
      /* Exterior was split — holes cannot be assigned reliably. */
      for (auto &part : exteriors)
        cleaned.push_back(std::move(part));
    }
  }

  band.polygons = std::move(cleaned);
}

void
SortBandsByAbsMid(ForecastLayer &layer) noexcept
{
  std::sort(layer.bands.begin(), layer.bands.end(),
            [](const WindBand &a, const WindBand &b) noexcept {
              const double mid_a = (a.min_ms + a.max_ms) * 0.5;
              const double mid_b = (b.min_ms + b.max_ms) * 0.5;
              return std::fabs(mid_a) < std::fabs(mid_b);
            });
}

} // namespace XCThermGeoJSON
