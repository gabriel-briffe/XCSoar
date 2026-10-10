// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeCpu.hpp"
#include "LogFile.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t FLAG_GROUND = 1;

struct MinHeap {
  std::vector<int> index;
  std::vector<float> req;

  [[nodiscard]] bool Empty() const noexcept { return index.empty(); }

  [[nodiscard]] std::size_t Size() const noexcept { return index.size(); }

  void Push(int i, float r) noexcept {
    index.push_back(i);
    req.push_back(r);
    Up(index.size() - 1);
  }

  std::pair<int, float> Pop() noexcept {
    const int i = index[0];
    const float r = req[0];
    const std::size_t last = index.size() - 1;
    if (last > 0) {
      index[0] = index[last];
      req[0] = req[last];
    }
    index.pop_back();
    req.pop_back();
    if (!index.empty())
      Down(0);
    return {i, r};
  }

private:
  void Up(std::size_t i) noexcept {
    while (i > 0) {
      const std::size_t parent = (i - 1) / 2;
      if (req[parent] <= req[i])
        return;
      Swap(parent, i);
      i = parent;
    }
  }

  void Down(std::size_t i) noexcept {
    const std::size_t n = index.size();
    while (true) {
      std::size_t best = i;
      const std::size_t left = i * 2 + 1;
      const std::size_t right = left + 1;
      if (left < n && req[left] < req[best])
        best = left;
      if (right < n && req[right] < req[best])
        best = right;
      if (best == i)
        return;
      Swap(i, best);
      i = best;
    }
  }

  void Swap(std::size_t a, std::size_t b) noexcept {
    std::swap(index[a], index[b]);
    std::swap(req[a], req[b]);
  }
};

[[gnu::pure]]
bool
CellBlocksRay(const std::vector<float> &elev,
              const std::vector<float> &best,
              unsigned width, unsigned height,
              int cx, int cy, int ox, int oy,
              float cell_x, float cell_y, float ratio) noexcept
{
  if (cx < 0 || cy < 0 || cx >= int(width) || cy >= int(height))
    return true;
  if (cx == ox && cy == oy)
    return false;
  const float dx = float(cx - ox) * cell_x;
  const float dy = float(cy - oy) * cell_y;
  const float glide_alt = best[std::size_t(oy) * width + ox] +
    std::sqrt(dx * dx + dy * dy) / ratio;
  return elev[std::size_t(cy) * width + cx] >= glide_alt;
}

bool
InView(const std::vector<float> &elev,
       const std::vector<float> &best,
       unsigned width, unsigned height,
       int x0, int y0, int ox, int oy,
       float cell_x, float cell_y, float ratio) noexcept
{
  if (ox < 0 || oy < 0 || ox >= int(width) || oy >= int(height))
    return false;
  if (x0 == ox && y0 == oy)
    return true;

  const int adx = std::abs(ox - x0);
  const int ady = std::abs(oy - y0);
  int x1 = x0;
  int y1 = y0;
  const int xstep = ox > x1 ? 1 : -1;
  const int ystep = oy > y1 ? 1 : -1;
  const int dx = adx;
  const int dy = ady;
  const int ddy = dy * 2;
  const int ddx = dx * 2;
  int error = dx;
  int errorprev = error;

  if (dx >= dy) {
    for (int s = 0; s < dx; ++s) {
      x1 += xstep;
      error += ddy;
      if (error > ddx) {
        y1 += ystep;
        error -= ddx;
        if (error + errorprev < ddx) {
          if (CellBlocksRay(elev, best, width, height,
                            x1, y1 - ystep, ox, oy, cell_x, cell_y, ratio))
            return false;
        } else if (error + errorprev > ddx) {
          if (CellBlocksRay(elev, best, width, height,
                            x1 - xstep, y1, ox, oy, cell_x, cell_y, ratio))
            return false;
        }
      }
      if (CellBlocksRay(elev, best, width, height,
                        x1, y1, ox, oy, cell_x, cell_y, ratio))
        return false;
      errorprev = error;
    }
  } else {
    for (int s = 0; s < dy; ++s) {
      y1 += ystep;
      error += ddx;
      if (error > ddy) {
        x1 += xstep;
        error -= ddy;
        if (error + errorprev < ddy) {
          if (CellBlocksRay(elev, best, width, height,
                            x1 - xstep, y1, ox, oy, cell_x, cell_y, ratio))
            return false;
        } else if (error + errorprev > ddy) {
          if (CellBlocksRay(elev, best, width, height,
                            x1, y1 - ystep, ox, oy, cell_x, cell_y, ratio))
            return false;
        }
      }
      if (CellBlocksRay(elev, best, width, height,
                        x1, y1, ox, oy, cell_x, cell_y, ratio))
        return false;
      errorprev = error;
    }
  }
  return true;
}

[[gnu::pure]]
float
ConeReq(const std::vector<float> &best, unsigned width,
        int ox, int oy, int x, int y,
        float cell_x, float cell_y, float ratio) noexcept
{
  const float dx = float(x - ox) * cell_x;
  const float dy = float(y - oy) * cell_y;
  return best[std::size_t(oy) * width + ox] +
    std::sqrt(dx * dx + dy * dy) / ratio;
}

} // namespace

bool
PropagateUpwardCpu(const GlideConeGrid &grid,
                   const std::function<bool()> &should_abort,
                   GlideConeResult &out,
                   bool *hit_iteration_cap) noexcept
{
  using Clock = std::chrono::steady_clock;
  const auto t0 = Clock::now();

  if (hit_iteration_cap != nullptr)
    *hit_iteration_cap = false;

  out.Clear();
  if (!grid.IsValid() || grid.glide_ratio <= 0) {
    LogFmt("GlideCone cone: cpu reject invalid grid seeds={} ld={:.1f}",
           grid.seeds.size(), grid.glide_ratio);
    return false;
  }

  const unsigned width = grid.width;
  const unsigned height = grid.height;
  const std::size_t n = std::size_t(width) * height;
  const float max_alt = grid.max_alt;
  const float ratio = float(grid.glide_ratio);
  const float cell_x = float(grid.cell_size_x_m > 0 ? grid.cell_size_x_m
                                                    : grid.cell_size_y_m);
  const float cell_y = float(grid.cell_size_y_m > 0 ? grid.cell_size_y_m
                                                    : grid.cell_size_x_m);

  std::vector<float> best(n, max_alt);
  std::vector<std::int32_t> origin_x(n, -1);
  std::vector<std::int32_t> origin_y(n, -1);
  std::vector<std::uint8_t> flags(n, 0);
  MinHeap heap;

  unsigned seed_n = 0;
  for (const auto &seed : grid.seeds) {
    if (seed.x < 0 || seed.y < 0 ||
        seed.x >= int(width) || seed.y >= int(height))
      continue;
    const std::size_t i = std::size_t(seed.y) * width + seed.x;
    best[i] = seed.alt;
    origin_x[i] = seed.x;
    origin_y[i] = seed.y;
    flags[i] = 0;
    heap.Push(int(i), seed.alt);
    ++seed_n;
  }

  if (heap.Empty()) {
    LogFmt("GlideCone cone: cpu no in-grid seeds {}x{} listed={}",
           width, height, grid.seeds.size());
    return false;
  }

  /* Dijkstra pops ≠ GPU parallel passes.  Run until the heap drains;
   * keep only a large safety bound (not Settings::iteration_cap). */
  const unsigned safety_cap = unsigned(n) * 16u + 1024u;
  LogFmt("GlideCone cone: cpu start {}x{} seeds={} ld={:.1f} "
         "cell={:.0f}x{:.0f}m max_alt={:.0f} safety_cap={}",
         width, height, seed_n, grid.glide_ratio,
         double(cell_x), double(cell_y), double(max_alt), safety_cap);

  unsigned iterations = 0;
  bool aborted = false;
  bool hit_cap = false;

  static constexpr int OFF[8][2] = {
    {-1, -1}, {0, -1}, {1, -1},
    {-1, 0}, {1, 0},
    {-1, 1}, {0, 1}, {1, 1},
  };

  while (!heap.Empty()) {
    if ((iterations & 1023u) == 0 && should_abort &&
        should_abort()) {
      aborted = true;
      break;
    }
    if (iterations >= safety_cap) {
      hit_cap = true;
      if (hit_iteration_cap != nullptr)
        *hit_iteration_cap = true;
      break;
    }

    const auto [index, req] = heap.Pop();
    ++iterations;
    const std::size_t ui = std::size_t(index);
    if (req > best[ui])
      continue;

    const int x = index % int(width);
    const int y = index / int(width);
    const int from_ox = origin_x[ui];
    const int from_oy = origin_y[ui];
    const bool from_ground = (flags[ui] & FLAG_GROUND) != 0;

    for (const auto &off : OFF) {
      const int nx = x + off[0];
      const int ny = y + off[1];
      if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height))
        continue;
      const std::size_t nidx = std::size_t(ny) * width + nx;

      int elected_ox;
      int elected_oy;
      if (from_ground) {
        elected_ox = x;
        elected_oy = y;
      } else if (from_ox >= 0 && from_oy >= 0 &&
                 InView(grid.elevation, best, width, height,
                        nx, ny, from_ox, from_oy, cell_x, cell_y, ratio)) {
        elected_ox = from_ox;
        elected_oy = from_oy;
      } else {
        elected_ox = x;
        elected_oy = y;
      }

      const float next = ConeReq(best, width, elected_ox, elected_oy,
                                 nx, ny, cell_x, cell_y, ratio);
      if (!(next < max_alt))
        continue;

      if (flags[nidx] & FLAG_GROUND) {
        float current_req = max_alt;
        if (origin_x[nidx] >= 0 && origin_y[nidx] >= 0)
          current_req = ConeReq(best, width, origin_x[nidx], origin_y[nidx],
                                nx, ny, cell_x, cell_y, ratio);
        if (next < current_req) {
          origin_x[nidx] = elected_ox;
          origin_y[nidx] = elected_oy;
        }
        continue;
      }

      if (!(next < best[nidx]))
        continue;

      const float surface = grid.elevation[nidx];
      if (next <= surface) {
        best[nidx] = surface;
        origin_x[nidx] = elected_ox;
        origin_y[nidx] = elected_oy;
        flags[nidx] = FLAG_GROUND;
        heap.Push(int(nidx), surface);
      } else {
        best[nidx] = next;
        origin_x[nidx] = elected_ox;
        origin_y[nidx] = elected_oy;
        flags[nidx] = 0;
        heap.Push(int(nidx), next);
      }
    }
  }

  const unsigned ms = unsigned(std::chrono::duration_cast<
    std::chrono::milliseconds>(Clock::now() - t0).count());

  if (aborted) {
    LogFmt("GlideCone cone: cpu aborted {}x{} iter={} heap={} {}ms",
           width, height, iterations, heap.Size(), ms);
    return false;
  }

  unsigned reachable = 0;
  unsigned ground_n = 0;
  out.width = width;
  out.height = height;
  out.altitudes = std::move(best);
  out.origin_x = std::move(origin_x);
  out.origin_y = std::move(origin_y);
  out.ground.assign(n, 0);
  for (std::size_t i = 0; i < n; ++i) {
    if (flags[i] & FLAG_GROUND) {
      out.ground[i] = 1;
      ++ground_n;
    }
    if (out.altitudes[i] < max_alt)
      ++reachable;
  }

  LogFmt("GlideCone cone: cpu done {}x{} iter={} reachable={} "
         "ground={} hit_cap={} heap_left={} {}ms",
         width, height, iterations, reachable, ground_n, hit_cap,
         heap.Size(), ms);
  return true;
}
