// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeField.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

[[gnu::pure]]
double
CellDistanceM(const GlideConeField &field, int x0, int y0,
              int x1, int y1) noexcept
{
  const double dx = double(x1 - x0) * field.cell_size_x_m;
  const double dy = double(y1 - y0) * field.cell_size_y_m;
  return std::hypot(dx, dy);
}

[[gnu::pure]]
bool
InGrid(const GlideConeResult &r, int x, int y) noexcept
{
  return x >= 0 && y >= 0 &&
    unsigned(x) < r.width && unsigned(y) < r.height;
}

[[gnu::pure]]
bool
IsGroundCell(const GlideConeResult &r, std::size_t index) noexcept
{
  return !r.ground.empty() && r.ground[index] != 0;
}

[[gnu::pure]]
float
SeedArrivalAltitude(const GlideConeField &field, int x, int y) noexcept
{
  for (const auto &s : field.seeds)
    if (s.x == x && s.y == y)
      return s.alt;

  const std::size_t index =
    std::size_t(y) * field.result.width + std::size_t(x);
  return field.result.altitudes[index];
}

/**
 * Prepare #GlideConeField::visit_stamp for a new walk.  Uses an epoch
 * counter so only touched cells are marked — no full-grid clear.
 */
void
BeginVisit(const GlideConeField &field, std::size_t count) noexcept
{
  if (field.visit_stamp.size() != count)
    field.visit_stamp.assign(count, 0);

  ++field.visit_epoch;
  if (field.visit_epoch == 0) {
    std::fill(field.visit_stamp.begin(), field.visit_stamp.end(), 0);
    field.visit_epoch = 1;
  }
}

[[gnu::pure]]
bool
WasVisited(const GlideConeField &field, std::size_t index) noexcept
{
  return field.visit_stamp[index] == field.visit_epoch;
}

void
MarkVisited(const GlideConeField &field, std::size_t index) noexcept
{
  field.visit_stamp[index] = field.visit_epoch;
}

} // anonymous namespace


GeoPoint
GlideConeField::CellToGeo(int x, int y) const noexcept
{
  const double fx = (x + 0.5) / double(result.width);
  const double fy = (y + 0.5) / double(result.height);
  const Angle lng = bounds.GetWest() + bounds.GetWidth() * fx;
  const Angle lat = bounds.GetNorth() - bounds.GetHeight() * fy;
  return GeoPoint(lng, lat);
}

bool
GlideConeField::GeoToCell(GeoPoint p, int &x, int &y) const noexcept
{
  const double width_native = bounds.GetWidth().Native();
  const double height_native = bounds.GetHeight().Native();
  if (width_native <= 0 || height_native <= 0)
    return false;

  const double fx = (p.longitude - bounds.GetWest()).Native() / width_native;
  const double fy = (bounds.GetNorth() - p.latitude).Native() / height_native;
  if (fx < 0 || fx >= 1 || fy < 0 || fy >= 1)
    return false;

  x = std::clamp(int(fx * result.width), 0, int(result.width) - 1);
  y = std::clamp(int(fy * result.height), 0, int(result.height) - 1);
  return true;
}

std::vector<GlideConeField::TraceCell>
GlideConeField::Trace(GeoPoint from) const noexcept
{
  std::vector<TraceCell> path;
  if (!IsValid())
    return path;

  int x, y;
  if (!GeoToCell(from, x, y))
    return path;

  const unsigned width = result.width;
  const unsigned height = result.height;

  /* bail out if the start cell is unreachable */
  const std::size_t start_index = std::size_t(y) * width + x;
  if (result.altitudes[start_index] >= max_alt)
    return path;

  const std::size_t count = std::size_t(width) * height;
  BeginVisit(*this, count);
  const unsigned max_steps = (width + height) * 2;

  for (unsigned step = 0; step < max_steps; ++step) {
    const std::size_t index = std::size_t(y) * width + x;
    if (WasVisited(*this, index))
      break;
    MarkVisited(*this, index);

    path.push_back({x, y});

    if (x == home_x && y == home_y)
      break;

    const std::int32_t nx = result.origin_x[index];
    const std::int32_t ny = result.origin_y[index];
    if (nx < 0 || ny < 0 || (nx == x && ny == y))
      break;

    x = nx;
    y = ny;
  }

  if (path.size() < 2)
    path.clear();

  return path;
}

std::optional<double>
GlideConeField::PathDistance(GeoPoint from) const noexcept
{
  const auto path = Trace(from);
  if (path.size() < 2)
    return std::nullopt;

  double distance_m = 0;
  for (std::size_t i = 1; i < path.size(); ++i) {
    const GeoPoint a = CellToGeo(path[i - 1].x, path[i - 1].y);
    const GeoPoint b = CellToGeo(path[i].x, path[i].y);
    if (!a.IsValid() || !b.IsValid())
      return std::nullopt;
    distance_m += a.DistanceS(b);
  }

  return distance_m;
}

bool
GlideConeField::IsDownhillGroundSegment(int from_x, int from_y,
                                        int to_x, int to_y) const noexcept
{
  if (!IsValid() || elevation.size() != result.altitudes.size())
    return false;
  if (!InGrid(result, from_x, from_y) || !InGrid(result, to_x, to_y))
    return false;

  const std::size_t from_i =
    std::size_t(from_y) * result.width + std::size_t(from_x);
  if (!IsGroundCell(result, from_i))
    return false;

  const std::size_t to_i =
    std::size_t(to_y) * result.width + std::size_t(to_x);
  return elevation[to_i] < elevation[from_i];
}

std::optional<double>
GlideConeField::RequiredAltitude(GeoPoint from) const noexcept
{
  if (!IsValid() || glide_ratio <= 0)
    return std::nullopt;

  int x, y;
  if (!GeoToCell(from, x, y))
    return std::nullopt;

  const unsigned width = result.width;
  const unsigned height = result.height;
  const std::size_t start_index = std::size_t(y) * width + x;
  if (result.altitudes[start_index] >= max_alt)
    return std::nullopt;

  /* air cell: stored altitude is the true required arrival height */
  if (!IsGroundCell(result, start_index))
    return double(result.altitudes[start_index]);

  /* ground cell: walk back to the first air cell (or the seed) */
  double distance_m = 0;
  int cx = x, cy = y;
  BeginVisit(*this, std::size_t(width) * height);
  const unsigned max_steps = (width + height) * 2;

  for (unsigned step = 0; step < max_steps; ++step) {
    const std::size_t index = std::size_t(cy) * width + cx;
    if (WasVisited(*this, index))
      return std::nullopt;
    MarkVisited(*this, index);

    if (!IsGroundCell(result, index))
      return double(result.altitudes[index]) + distance_m / glide_ratio;

    const std::int32_t nx = result.origin_x[index];
    const std::int32_t ny = result.origin_y[index];
    if (nx < 0 || ny < 0 || !InGrid(result, nx, ny) ||
        (nx == cx && ny == cy)) {
      /* ground all the way to the seed */
      return double(SeedArrivalAltitude(*this, cx, cy)) +
        distance_m / glide_ratio;
    }

    distance_m += CellDistanceM(*this, cx, cy, nx, ny);
    cx = nx;
    cy = ny;
  }

  return std::nullopt;
}

/* marching squares tables (see gpu-MC contours.js) */
namespace {
constexpr int CORNER_OFFSETS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
constexpr int EDGE_VERTICES[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};

struct EdgePair { int a, b; };
struct CaseSegments {
  unsigned count;
  EdgePair seg[2];
};

constexpr CaseSegments MS_SEGMENTS[16] = {
  {0, {}},
  {1, {{3, 0}}},
  {1, {{0, 1}}},
  {1, {{3, 1}}},
  {1, {{1, 2}}},
  {2, {{3, 0}, {1, 2}}}, /* saddle; pairing may flip via asymptotic decider */
  {1, {{0, 2}}},
  {1, {{3, 2}}},
  {1, {{2, 3}}},
  {1, {{2, 0}}},
  {2, {{0, 1}, {2, 3}}}, /* saddle; pairing may flip via asymptotic decider */
  {1, {{2, 1}}},
  {1, {{1, 3}}},
  {1, {{1, 0}}},
  {1, {{0, 3}}},
  {0, {}},
};

[[gnu::pure]]
GeoPoint Lerp(GeoPoint a, GeoPoint b, double t) noexcept
{
  return GeoPoint(a.longitude + (b.longitude - a.longitude) * t,
                  a.latitude + (b.latitude - a.latitude) * t);
}

/**
 * Exit edge for a cell given the entry edge.  Saddle cases (5, 10) use
 * the asymptotic decider so adjacent cells share one continuous path.
 */
void
CasePairs(unsigned case_index, const std::array<float, 4> &values,
          float level, EdgePair pairs[2], unsigned &n) noexcept
{
  n = 0;
  if (case_index == 5 || case_index == 10) {
    const float center =
      (values[0] + values[1] + values[2] + values[3]) * 0.25f;
    const bool flip = center >= level;
    if (case_index == 5) {
      if (flip) {
        pairs[0] = {0, 1};
        pairs[1] = {2, 3};
      } else {
        pairs[0] = {3, 0};
        pairs[1] = {1, 2};
      }
    } else if (flip) {
      pairs[0] = {3, 0};
      pairs[1] = {1, 2};
    } else {
      pairs[0] = {0, 1};
      pairs[1] = {2, 3};
    }
    n = 2;
  } else {
    const CaseSegments &cs = MS_SEGMENTS[case_index];
    n = cs.count;
    for (unsigned s = 0; s < n; ++s)
      pairs[s] = cs.seg[s];
  }
}

/**
 * Pack unique edge indices:
 *   H(ci, rj): horizontal between corners (ci,rj)-(ci+1,rj)
 *              ci in [0,w-2], rj in [0,h-1]  →  index rj*(w-1)+ci
 *   V(ci, rj): vertical between (ci,rj)-(ci,rj+1)
 *              ci in [0,w-1], rj in [0,h-2]  →  after all H
 */
[[gnu::const]]
std::size_t
UniqueEdgeId(unsigned cell_i, unsigned cell_j, int edge,
             unsigned corner_w, unsigned corner_h) noexcept
{
  const std::size_t n_horiz =
    std::size_t(corner_w - 1) * corner_h;

  switch (edge) {
  case 0: /* top H(cell_i, cell_j) */
    return std::size_t(cell_j) * (corner_w - 1) + cell_i;
  case 2: /* bottom H(cell_i, cell_j+1) */
    return std::size_t(cell_j + 1) * (corner_w - 1) + cell_i;
  case 1: /* right V(cell_i+1, cell_j) */
    return n_horiz + std::size_t(cell_j) * corner_w + (cell_i + 1);
  case 3: /* left V(cell_i, cell_j) */
    return n_horiz + std::size_t(cell_j) * corner_w + cell_i;
  default:
    return 0;
  }
}

[[gnu::const]]
std::size_t
EdgeCount(unsigned corner_w, unsigned corner_h) noexcept
{
  return std::size_t(corner_w - 1) * corner_h +
    std::size_t(corner_w) * (corner_h - 1);
}

/** One marching-squares segment with UniqueEdgeId endpoints for stitching. */
struct RawSeg {
  std::size_t e0, e1;
  GeoPoint p0, p1;
};

/**
 * Chain raw segments that share UniqueEdgeId endpoints into polylines.
 * Every input segment appears in exactly one output line.
 */
void
StitchRawSegments(const std::vector<RawSeg> &segs, std::size_t n_edges,
                  int level,
                  std::vector<GlideConeField::ContourLine> &out) noexcept
{
  if (segs.empty())
    return;

  std::vector<std::vector<std::pair<unsigned, uint8_t>>> adj(n_edges);
  for (unsigned i = 0; i < segs.size(); ++i) {
    adj[segs[i].e0].emplace_back(i, 0);
    adj[segs[i].e1].emplace_back(i, 1);
  }

  const auto edge_of = [&](unsigned si, uint8_t end) noexcept {
    return end == 0 ? segs[si].e0 : segs[si].e1;
  };
  const auto point_of = [&](unsigned si, uint8_t end) noexcept {
    return end == 0 ? segs[si].p0 : segs[si].p1;
  };

  std::vector<char> used(segs.size(), 0);

  for (unsigned seed = 0; seed < segs.size(); ++seed) {
    if (used[seed])
      continue;
    used[seed] = 1;

    std::vector<GeoPoint> forward;
    forward.push_back(segs[seed].p0);
    forward.push_back(segs[seed].p1);

    const auto grow = [&](unsigned cur, uint8_t at,
                          std::vector<GeoPoint> &pts) noexcept {
      for (;;) {
        const std::size_t e = edge_of(cur, at);
        unsigned next = ~0u;
        uint8_t next_at = 0;
        for (const auto &[sj, ej] : adj[e]) {
          if (sj == cur || used[sj])
            continue;
          next = sj;
          next_at = ej;
          break;
        }
        if (next == ~0u)
          break;
        used[next] = 1;
        const uint8_t far = uint8_t(next_at ^ 1);
        pts.push_back(point_of(next, far));
        cur = next;
        at = far;
      }
    };

    grow(seed, 1, forward);

    std::vector<GeoPoint> backward;
    grow(seed, 0, backward);

    std::vector<GeoPoint> line;
    line.reserve(backward.size() + forward.size());
    for (auto it = backward.rbegin(); it != backward.rend(); ++it)
      line.push_back(*it);
    for (const GeoPoint &p : forward)
      line.push_back(p);

    std::vector<GeoPoint> deduped;
    deduped.reserve(line.size());
    for (const GeoPoint &p : line)
      if (deduped.empty() ||
          deduped.back().longitude != p.longitude ||
          deduped.back().latitude != p.latitude)
        deduped.push_back(p);

    if (deduped.size() >= 2)
      out.push_back({std::move(deduped), level});
  }
}
} // anonymous namespace

void
GlideConeField::BuildContours(double interval_m) noexcept
{
  contour_lines.clear();
  if (!IsValid() || interval_m <= 0)
    return;

  const unsigned w = result.width;
  const unsigned h = result.height;
  if (w < 2 || h < 2)
    return;

  const unsigned cells_w = w - 1;
  const unsigned cells_h = h - 1;
  const float max_alt_f = max_alt;
  const bool have_ground = !result.ground.empty();

  const auto valid_alt = [&](unsigned i, unsigned j) -> std::optional<float> {
    const std::size_t idx = std::size_t(j) * w + i;
    if (have_ground && result.ground[idx])
      return std::nullopt;
    if (result.origin_x[idx] < 0)
      return std::nullopt;
    const float a = result.altitudes[idx];
    if (!(a < max_alt_f))
      return std::nullopt;
    return a;
  };

  float max_reachable = 0;
  for (std::size_t i = 0; i < result.altitudes.size(); ++i) {
    if ((!have_ground || !result.ground[i]) && result.origin_x[i] >= 0) {
      const float a = result.altitudes[i];
      if (a < max_alt_f && a > max_reachable)
        max_reachable = a;
    }
  }

  const int max_level = int(std::floor(max_reachable / interval_m) * interval_m);
  const std::size_t n_edges = EdgeCount(w, h);

  for (int level = int(interval_m); level <= max_level;
       level += int(interval_m)) {
    const float flevel = float(level);

    std::vector<std::optional<std::array<float, 4>>> cell_values(
      std::size_t(cells_w) * cells_h);
    std::vector<unsigned char> cell_case(std::size_t(cells_w) * cells_h, 0);

    for (unsigned j = 0; j < cells_h; ++j) {
      for (unsigned i = 0; i < cells_w; ++i) {
        std::array<float, 4> values;
        bool ok = true;
        for (unsigned c = 0; c < 4; ++c) {
          const auto v = valid_alt(i + CORNER_OFFSETS[c][0],
                                   j + CORNER_OFFSETS[c][1]);
          if (!v) {
            ok = false;
            break;
          }
          values[c] = *v;
        }
        if (!ok)
          continue;

        unsigned case_index = 0;
        for (unsigned c = 0; c < 4; ++c)
          if (values[c] >= flevel)
            case_index |= 1u << c;
        if (case_index == 0 || case_index == 15)
          continue;

        const std::size_t cidx = std::size_t(j) * cells_w + i;
        cell_values[cidx] = values;
        cell_case[cidx] = (unsigned char)case_index;
      }
    }

    const auto edge_point = [&](unsigned i, unsigned j,
                                int edge) -> std::optional<GeoPoint> {
      const std::size_t cidx = std::size_t(j) * cells_w + i;
      if (!cell_values[cidx])
        return std::nullopt;
      const auto &values = *cell_values[cidx];
      const int a = EDGE_VERTICES[edge][0];
      const int b = EDGE_VERTICES[edge][1];
      const float z1 = values[a], z2 = values[b];
      if (z1 == z2)
        return std::nullopt;
      const double t = (double(flevel) - z1) / (z2 - z1);
      if (t < 0 || t > 1)
        return std::nullopt;

      std::array<GeoPoint, 4> corners;
      for (unsigned c = 0; c < 4; ++c)
        corners[c] = CellToGeo(int(i + CORNER_OFFSETS[c][0]),
                               int(j + CORNER_OFFSETS[c][1]));
      return Lerp(corners[a], corners[b], t);
    };

    std::vector<RawSeg> segs;
    for (unsigned j = 0; j < cells_h; ++j) {
      for (unsigned i = 0; i < cells_w; ++i) {
        const std::size_t cidx = std::size_t(j) * cells_w + i;
        if (!cell_values[cidx])
          continue;

        EdgePair pairs[2];
        unsigned n = 0;
        CasePairs(cell_case[cidx], *cell_values[cidx], flevel, pairs, n);
        for (unsigned s = 0; s < n; ++s) {
          const auto p0 = edge_point(i, j, pairs[s].a);
          const auto p1 = edge_point(i, j, pairs[s].b);
          if (!p0 || !p1)
            continue;
          segs.push_back({
            UniqueEdgeId(i, j, pairs[s].a, w, h),
            UniqueEdgeId(i, j, pairs[s].b, w, h),
            *p0, *p1,
          });
        }
      }
    }

    StitchRawSegments(segs, n_edges, level, contour_lines);
  }
}
