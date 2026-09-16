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

  std::vector<bool> visited(std::size_t(width) * height, false);
  const unsigned max_steps = (width + height) * 2;

  for (unsigned step = 0; step < max_steps; ++step) {
    const std::size_t index = std::size_t(y) * width + x;
    if (visited[index])
      break;
    visited[index] = true;

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
  std::vector<bool> visited(std::size_t(width) * height, false);
  const unsigned max_steps = (width + height) * 2;

  for (unsigned step = 0; step < max_steps; ++step) {
    const std::size_t index = std::size_t(cy) * width + cx;
    if (visited[index])
      return std::nullopt;
    visited[index] = true;

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
[[gnu::pure]]
int ExitEdge(unsigned case_index, int entry,
             const std::array<float, 4> &values, float level) noexcept
{
  EdgePair pairs[2];
  unsigned n = 0;

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

  for (unsigned s = 0; s < n; ++s) {
    if (pairs[s].a == entry)
      return pairs[s].b;
    if (pairs[s].b == entry)
      return pairs[s].a;
  }
  return -1;
}

struct NeighborCell {
  unsigned i, j;
  int entry;
};

/**
 * Step across @p exit_edge of cell (i,j) into the adjacent marching-squares
 * cell; the shared edge becomes @c entry there.
 */
[[gnu::pure]]
std::optional<NeighborCell>
StepAcross(unsigned i, unsigned j, int exit_edge,
           unsigned cells_w, unsigned cells_h) noexcept
{
  switch (exit_edge) {
  case 0: /* top → cell above, enter bottom */
    if (j == 0)
      return std::nullopt;
    return NeighborCell{i, j - 1, 2};
  case 1: /* right → cell right, enter left */
    if (i + 1 >= cells_w)
      return std::nullopt;
    return NeighborCell{i + 1, j, 3};
  case 2: /* bottom → cell below, enter top */
    if (j + 1 >= cells_h)
      return std::nullopt;
    return NeighborCell{i, j + 1, 0};
  case 3: /* left → cell left, enter right */
    if (i == 0)
      return std::nullopt;
    return NeighborCell{i - 1, j, 1};
  default:
    return std::nullopt;
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

    /* per-cell corner values / case; nullopt cell = not marchable */
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

    std::vector<char> visited(n_edges, 0);

    const auto follow = [&](unsigned i, unsigned j,
                            int entry) -> std::vector<GeoPoint> {
      std::vector<GeoPoint> pts;
      for (;;) {
        const std::size_t cidx = std::size_t(j) * cells_w + i;
        if (!cell_values[cidx])
          break;

        const unsigned cas = cell_case[cidx];
        const int exit_e = ExitEdge(cas, entry, *cell_values[cidx], flevel);
        if (exit_e < 0)
          break;

        const std::size_t id_in = UniqueEdgeId(i, j, entry, w, h);
        if (visited[id_in])
          break;

        const auto p_in = edge_point(i, j, entry);
        const auto p_out = edge_point(i, j, exit_e);
        if (!p_in || !p_out)
          break;

        if (pts.empty())
          pts.push_back(*p_in);
        visited[id_in] = 1;

        pts.push_back(*p_out);
        visited[UniqueEdgeId(i, j, exit_e, w, h)] = 1;

        const auto next = StepAcross(i, j, exit_e, cells_w, cells_h);
        if (!next)
          break;
        if (visited[UniqueEdgeId(next->i, next->j, next->entry, w, h)])
          break; /* closed ring */

        i = next->i;
        j = next->j;
        entry = next->entry;
      }
      return pts;
    };

    for (unsigned j = 0; j < cells_h; ++j) {
      for (unsigned i = 0; i < cells_w; ++i) {
        const std::size_t cidx = std::size_t(j) * cells_w + i;
        if (!cell_values[cidx])
          continue;

        const unsigned cas = cell_case[cidx];
        /* try each edge that participates in this case */
        for (int edge = 0; edge < 4; ++edge) {
          if (ExitEdge(cas, edge, *cell_values[cidx], flevel) < 0)
            continue;

          const std::size_t seed_id = UniqueEdgeId(i, j, edge, w, h);
          if (visited[seed_id])
            continue;

          auto forward = follow(i, j, edge);

          /* Closed ring: first and last meet on the seed edge. */
          const bool closed =
            forward.size() >= 3 &&
            forward.front().longitude == forward.back().longitude &&
            forward.front().latitude == forward.back().latitude;

          std::vector<GeoPoint> line;
          if (closed) {
            line = std::move(forward);
          } else {
            /* Open contour: walk the other way from the seed. */
            visited[seed_id] = 0;
            std::vector<GeoPoint> backward;
            if (const auto back =
                  StepAcross(i, j, edge, cells_w, cells_h)) {
              if (!visited[UniqueEdgeId(back->i, back->j,
                                        back->entry, w, h)])
                backward = follow(back->i, back->j, back->entry);
            }
            visited[seed_id] = 1;

            line.reserve(backward.size() + forward.size());
            for (auto it = backward.rbegin(); it != backward.rend(); ++it)
              line.push_back(*it);
            const std::size_t skip =
              (!backward.empty() && !forward.empty()) ? 1 : 0;
            for (std::size_t k = skip; k < forward.size(); ++k)
              line.push_back(forward[k]);
          }

          /* drop consecutive duplicates */
          std::vector<GeoPoint> deduped;
          deduped.reserve(line.size());
          for (const GeoPoint &p : line)
            if (deduped.empty() ||
                deduped.back().longitude != p.longitude ||
                deduped.back().latitude != p.latitude)
              deduped.push_back(p);

          if (deduped.size() >= 2)
            contour_lines.push_back({std::move(deduped), level});
        }
      }
    }
  }
}
