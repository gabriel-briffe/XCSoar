// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeDownward.hpp"
#include "LogFile.hpp"

#ifdef HAVE_GLES_COMPUTE

#include "ui/opengl/GLESCompute.hpp"
#include "GlideConeTiming.hpp"

#include <GLES3/gl31.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

struct GpuCell {
  float alt;
  std::int32_t ox;
  std::int32_t oy;
  std::uint32_t flags;
};

static_assert(sizeof(GpuCell) == 16, "GpuCell must match std430 layout");

/** Cell is in the optional area (arrival above the cone floor). */
constexpr std::uint32_t FLAG_OPTION = 1u;
constexpr std::uint32_t FLAG_CHANGED = 2u;

/**
 * Flush the command stream this often without waiting so the driver
 * does not grow an unbounded queue before the first check.
 */
constexpr unsigned FLUSH_EVERY = 64;

using Clock = std::chrono::steady_clock;

[[nodiscard]] unsigned
ElapsedMs(Clock::time_point t0) noexcept
{
  return unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(
    Clock::now() - t0).count());
}

/*
 * Downward optional-area propagate (GLES 3.1).
 *
 * Cells that stay above the stored cone floor are Options.  The
 * wavefront grows only through Option neighbours — no GC freeze.
 * Arrivals that would fall at/below the floor are simply not written,
 * so a better path can still fill that cell later.  LOS uses the same
 * stored floors (not proof-lowered ground heights).
 *
 * No atomics here — change counting is a separate sum pass, run only
 * on convergence-check iterations.
 */
constexpr char DOWNWARD_SHADER[] = R"GLSL(#version 310 es
precision highp float;
precision highp int;

layout(local_size_x = 8, local_size_y = 8) in;

struct Cell {
  float alt;
  int ox;
  int oy;
  uint flags;
};

layout(std430, binding = 0) readonly buffer FloorBuf { float floors[]; };
layout(std430, binding = 1) readonly buffer CellInBuf { Cell cin[]; };
layout(std430, binding = 2) buffer CellOutBuf { Cell cout[]; };

uniform int uWidth;
uniform int uHeight;
uniform float uCellSizeX;
uniform float uCellSizeY;
uniform float uGlideRatio;
uniform float uMaxAlt;

const uint FLAG_OPTION = 1u;
const uint FLAG_CHANGED = 2u;

int idx(int x, int y) { return y * uWidth + x; }

bool inBounds(int x, int y) {
  return x >= 0 && y >= 0 && x < uWidth && y < uHeight;
}

bool originValid(int ox, int oy) { return inBounds(ox, oy); }

bool hasStoredOrigin(int ox, int oy) {
  return originValid(ox, oy) && !(ox == -1 && oy == -1);
}

bool hasConeFloor(int i) { return floors[i] < uMaxAlt; }

bool isOptionAt(int x, int y) {
  if (!inBounds(x, y))
    return false;
  return (cin[idx(x, y)].flags & FLAG_OPTION) != 0u;
}

bool isOptionCell(uint flags) { return (flags & FLAG_OPTION) != 0u; }
bool wasModified(uint flags) { return (flags & FLAG_CHANGED) != 0u; }

uint packFlags(bool option, bool changed) {
  uint f = 0u;
  if (option) f = f | FLAG_OPTION;
  if (changed) f = f | FLAG_CHANGED;
  return f;
}

bool cellBlocksRay(int cx, int cy, int ox, int oy, float originAlt) {
  if (!inBounds(cx, cy))
    return true;
  if (cx == ox && cy == oy)
    return false;
  int i = idx(cx, cy);
  if (!hasConeFloor(i))
    return false;
  float dx = float(cx - ox) * uCellSizeX;
  float dy = float(cy - oy) * uCellSizeY;
  float descent = originAlt - sqrt(dx * dx + dy * dy) / uGlideRatio;
  return floors[i] >= descent;
}

bool isInViewToOrigin(int x0, int y0, int targetOx, int targetOy) {
  if (!originValid(targetOx, targetOy))
    return false;
  if (x0 == targetOx && y0 == targetOy)
    return true;
  float originAlt = cin[idx(targetOx, targetOy)].alt;
  int adx = abs(targetOx - x0);
  int ady = abs(targetOy - y0);
  int x1 = x0;
  int y1 = y0;
  int xstep = targetOx > x1 ? 1 : -1;
  int ystep = targetOy > y1 ? 1 : -1;
  int dx = adx;
  int dy = ady;
  int ddy = dy * 2;
  int ddx = dx * 2;
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
          if (cellBlocksRay(x1, y1 - ystep, targetOx, targetOy, originAlt))
            return false;
        } else if (error + errorprev > ddx) {
          if (cellBlocksRay(x1 - xstep, y1, targetOx, targetOy, originAlt))
            return false;
        }
      }
      if (cellBlocksRay(x1, y1, targetOx, targetOy, originAlt))
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
          if (cellBlocksRay(x1 - xstep, y1, targetOx, targetOy, originAlt))
            return false;
        } else if (error + errorprev > ddy) {
          if (cellBlocksRay(x1, y1 - ystep, targetOx, targetOy, originAlt))
            return false;
        }
      }
      if (cellBlocksRay(x1, y1, targetOx, targetOy, originAlt))
        return false;
      errorprev = error;
    }
  }
  return true;
}

float arrivalFrom(int ox, int oy, int x, int y) {
  float dx = float(x - ox) * uCellSizeX;
  float dy = float(y - oy) * uCellSizeY;
  return cin[idx(ox, oy)].alt - sqrt(dx * dx + dy * dy) / uGlideRatio;
}

bool neighborIsActiveOption(int nx, int ny, int myOx, int myOy) {
  if (!inBounds(nx, ny))
    return false;
  uint nflags = cin[idx(nx, ny)].flags;
  if (!isOptionCell(nflags) || !wasModified(nflags))
    return false;
  int nox = cin[idx(nx, ny)].ox;
  int noy = cin[idx(nx, ny)].oy;
  return nox != myOx || noy != myOy;
}

void consider(int nx, int ny, int x, int y, int myOx, int myOy,
              inout float bestArrival, inout int bestOx, inout int bestOy) {
  if (!neighborIsActiveOption(nx, ny, myOx, myOy))
    return;
  int electedX = nx;
  int electedY = ny;
  int pox = cin[idx(nx, ny)].ox;
  int poy = cin[idx(nx, ny)].oy;
  if (isInViewToOrigin(x, y, pox, poy)) {
    electedX = pox;
    electedY = poy;
  }
  if (!originValid(electedX, electedY) || !isOptionAt(electedX, electedY))
    return;
  float arrival = arrivalFrom(electedX, electedY, x, y);
  if (arrival > bestArrival) {
    bestArrival = arrival;
    bestOx = electedX;
    bestOy = electedY;
  }
}

void passthrough(int i, int ox, int oy, float alt, uint flags) {
  cout[i].alt = alt;
  cout[i].ox = ox;
  cout[i].oy = oy;
  cout[i].flags = flags & FLAG_OPTION;
}

void main() {
  int x = int(gl_GlobalInvocationID.x);
  int y = int(gl_GlobalInvocationID.y);
  if (!inBounds(x, y))
    return;
  int i = idx(x, y);
  int myOx = cin[i].ox;
  int myOy = cin[i].oy;
  float curAlt = cin[i].alt;
  uint curFlags = cin[i].flags;

  bool hasNeighbor = neighborIsActiveOption(x - 1, y - 1, myOx, myOy)
    || neighborIsActiveOption(x, y - 1, myOx, myOy)
    || neighborIsActiveOption(x + 1, y - 1, myOx, myOy)
    || neighborIsActiveOption(x - 1, y, myOx, myOy)
    || neighborIsActiveOption(x + 1, y, myOx, myOy)
    || neighborIsActiveOption(x - 1, y + 1, myOx, myOy)
    || neighborIsActiveOption(x, y + 1, myOx, myOy)
    || neighborIsActiveOption(x + 1, y + 1, myOx, myOy);
  if (!hasNeighbor) {
    passthrough(i, myOx, myOy, curAlt, curFlags);
    return;
  }

  float bestArrival = curAlt;
  int bestOx = myOx;
  int bestOy = myOy;
  if (!hasStoredOrigin(myOx, myOy))
    bestArrival = -1e30;

  consider(x - 1, y - 1, x, y, myOx, myOy, bestArrival, bestOx, bestOy);
  consider(x, y - 1, x, y, myOx, myOy, bestArrival, bestOx, bestOy);
  consider(x + 1, y - 1, x, y, myOx, myOy, bestArrival, bestOx, bestOy);
  consider(x - 1, y, x, y, myOx, myOy, bestArrival, bestOx, bestOy);
  consider(x + 1, y, x, y, myOx, myOy, bestArrival, bestOx, bestOy);
  consider(x - 1, y + 1, x, y, myOx, myOy, bestArrival, bestOx, bestOy);
  consider(x, y + 1, x, y, myOx, myOy, bestArrival, bestOx, bestOy);
  consider(x + 1, y + 1, x, y, myOx, myOy, bestArrival, bestOx, bestOy);

  if (hasStoredOrigin(myOx, myOy) && bestArrival <= curAlt) {
    passthrough(i, myOx, myOy, curAlt, curFlags);
    return;
  }
  if (bestArrival <= -1e20) {
    passthrough(i, myOx, myOy, curAlt, curFlags);
    return;
  }

  /* No upward-cone value (capped / unreachable): not an Option. */
  if (!hasConeFloor(i)) {
    passthrough(i, myOx, myOy, curAlt, curFlags);
    return;
  }
  /* Below / on the cone floor: leave empty so a better path can fill. */
  if (bestArrival <= floors[i]) {
    passthrough(i, myOx, myOy, curAlt, curFlags);
    return;
  }

  cout[i].alt = bestArrival;
  cout[i].ox = bestOx;
  cout[i].oy = bestOy;
  bool changed = bestOx != myOx || bestOy != myOy
    || bestArrival > curAlt
    || !isOptionCell(curFlags);
  cout[i].flags = packFlags(true, changed);
}
)GLSL";

/** gpu-MC CHANGED_SUM_SHADER: count FLAG_CHANGED on the cell buffer. */
constexpr char CHANGED_SUM_SHADER[] = R"GLSL(#version 310 es
precision highp int;

layout(local_size_x = 8, local_size_y = 8) in;

struct Cell {
  float alt;
  int ox;
  int oy;
  uint flags;
};

layout(std430, binding = 0) readonly buffer CellBuf { Cell cells[]; };
layout(std430, binding = 1) buffer ChangeBuf { coherent uint change_count; };

uniform int uWidth;
uniform int uHeight;

const uint FLAG_CHANGED = 2u;

void main() {
  int x = int(gl_GlobalInvocationID.x);
  int y = int(gl_GlobalInvocationID.y);
  if (x >= uWidth || y >= uHeight)
    return;
  int i = y * uWidth + x;
  if ((cells[i].flags & FLAG_CHANGED) != 0u)
    atomicAdd(change_count, 1u);
}
)GLSL";

struct Pool {
  GLuint program = 0;
  GLuint sum_program = 0;
  GLuint floors = 0;
  GLuint cell_a = 0;
  GLuint cell_b = 0;
  GLuint change = 0;
  GLuint change_read = 0;
  GLuint cell_read = 0;
  GLint loc_width = -1;
  GLint loc_height = -1;
  GLint loc_cell_x = -1;
  GLint loc_cell_y = -1;
  GLint loc_ratio = -1;
  GLint loc_max_alt = -1;
  GLint sum_loc_width = -1;
  GLint sum_loc_height = -1;
  std::size_t count = 0;
  GLsizeiptr cell_bytes = 0;
};

Pool pool;

void
WaitGpuFence() noexcept
{
  const GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (fence != nullptr) {
    glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
    glDeleteSync(fence);
  } else {
    glFinish();
  }
}

#if GLIDECONE_TIMING
namespace GT = GlideConeTiming;

#if GLIDECONE_TIMING_GPU
/** GPU timer-query categories (GT::GpuTimerSet kinds). */
constexpr unsigned GPU_PROPAGATE = 0;
constexpr unsigned GPU_SUM = 1;
#endif

/**
 * Sub-step times of the last ReadChangeCount() call, microseconds.
 * Only touched on the GLES compute thread.
 */
struct ReadChangeTimes {
  std::uint64_t copy_us = 0;
  std::uint64_t wait_us = 0;
  std::uint64_t map_us = 0;
};

ReadChangeTimes last_read_change;

/** EnsurePrograms() time and buffer (re)allocation flag of the last
    EnsurePool() call. */
std::uint64_t last_ensure_programs_us = 0;
bool last_pool_realloc = false;
#endif

GLuint
Compile(const char *src, const char *label) noexcept
{
  const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint status = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    LogFmt("GlideCone options: {} compile failed: {}", label, log);
    glDeleteShader(shader);
    return 0;
  }
  const GLuint program = glCreateProgram();
  glAttachShader(program, shader);
  glLinkProgram(program);
  glDeleteShader(shader);
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    char log[512];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    LogFmt("GlideCone options: {} link failed: {}", label, log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

void
DeleteBuffers() noexcept
{
  const GLuint bufs[6] = {
    pool.floors, pool.cell_a, pool.cell_b,
    pool.change, pool.change_read, pool.cell_read,
  };
  bool any = false;
  for (GLuint b : bufs)
    if (b != 0)
      any = true;
  if (any)
    glDeleteBuffers(6, bufs);
  pool.floors = pool.cell_a = pool.cell_b = 0;
  pool.change = pool.change_read = pool.cell_read = 0;
  pool.count = 0;
  pool.cell_bytes = 0;
}

bool
EnsurePrograms() noexcept
{
  if (pool.program == 0) {
    pool.program = Compile(DOWNWARD_SHADER, "propagate");
    if (pool.program == 0)
      return false;
    pool.loc_width = glGetUniformLocation(pool.program, "uWidth");
    pool.loc_height = glGetUniformLocation(pool.program, "uHeight");
    pool.loc_cell_x = glGetUniformLocation(pool.program, "uCellSizeX");
    pool.loc_cell_y = glGetUniformLocation(pool.program, "uCellSizeY");
    pool.loc_ratio = glGetUniformLocation(pool.program, "uGlideRatio");
    pool.loc_max_alt = glGetUniformLocation(pool.program, "uMaxAlt");
  }
  if (pool.sum_program == 0) {
    pool.sum_program = Compile(CHANGED_SUM_SHADER, "sum");
    if (pool.sum_program == 0)
      return false;
    pool.sum_loc_width = glGetUniformLocation(pool.sum_program, "uWidth");
    pool.sum_loc_height = glGetUniformLocation(pool.sum_program, "uHeight");
  }
  return true;
}

bool
EnsurePool(std::size_t count) noexcept
{
  GLIDECONE_TIMING_ONLY(last_pool_realloc = false;
                        const std::uint64_t t_programs = GT::NowUs();)
  if (!EnsurePrograms())
    return false;
  GLIDECONE_TIMING_ONLY(last_ensure_programs_us = GT::SinceUs(t_programs);)

  if (pool.count == count && pool.floors != 0)
    return true;

  GLIDECONE_TIMING_ONLY(last_pool_realloc = true;)
  DeleteBuffers();
  GLuint bufs[6] = {};
  glGenBuffers(6, bufs);
  pool.floors = bufs[0];
  pool.cell_a = bufs[1];
  pool.cell_b = bufs[2];
  pool.change = bufs[3];
  pool.change_read = bufs[4];
  pool.cell_read = bufs[5];

  const GLsizeiptr floats = GLsizeiptr(count * sizeof(float));
  const GLsizeiptr cells = GLsizeiptr(count * sizeof(GpuCell));
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.floors);
  glBufferData(GL_SHADER_STORAGE_BUFFER, floats, nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.cell_a);
  glBufferData(GL_SHADER_STORAGE_BUFFER, cells, nullptr, GL_DYNAMIC_COPY);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.cell_b);
  glBufferData(GL_SHADER_STORAGE_BUFFER, cells, nullptr, GL_DYNAMIC_COPY);
  const std::uint32_t zero = 0;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.change);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(zero), &zero,
               GL_DYNAMIC_COPY);
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.change_read);
  glBufferData(GL_COPY_WRITE_BUFFER, sizeof(zero), nullptr, GL_STREAM_READ);
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.cell_read);
  glBufferData(GL_COPY_WRITE_BUFFER, cells, nullptr, GL_STREAM_READ);
  pool.count = count;
  pool.cell_bytes = cells;
  return true;
}

/**
 * Read the change counter via a staging copy (gpu-MC changeReadBuffer).
 * Returns false if the map fails (caller should keep iterating).
 */
bool
ReadChangeCount(std::uint32_t &changes) noexcept
{
  GLIDECONE_TIMING_ONLY(last_read_change = {};
                        std::uint64_t t = GT::NowUs();)
  glBindBuffer(GL_COPY_READ_BUFFER, pool.change);
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.change_read);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                      0, 0, sizeof(changes));
  GLIDECONE_TIMING_ONLY(last_read_change.copy_us = GT::SinceUs(t);
                        t = GT::NowUs();)
  /* CPU-GPU sync: blocks until every queued propagate/sum dispatch and
     the copy above have finished on the GPU. */
  WaitGpuFence();
  GLIDECONE_TIMING_ONLY(last_read_change.wait_us = GT::SinceUs(t);
                        t = GT::NowUs();)
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.change_read);
  const auto *mapped = static_cast<const std::uint32_t *>(
    glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, sizeof(changes),
                     GL_MAP_READ_BIT));
  if (mapped == nullptr) {
    GLIDECONE_TIMING_ONLY(last_read_change.map_us = GT::SinceUs(t);)
    return false;
  }
  changes = *mapped;
  glUnmapBuffer(GL_COPY_WRITE_BUFFER);
  GLIDECONE_TIMING_ONLY(last_read_change.map_us = GT::SinceUs(t);)
  return true;
}

/**
 * First convergence check for options: longest no-terrain reach from
 * the glider in cells = (path to airport + ½·margin·L/D) / cell.
 * Capped by the old half-grid bound so a huge margin cannot delay the
 * first check past what we already know is safe.
 */
unsigned
OptionsFirstCheckAt(unsigned width, unsigned height,
                    double cell_x, double cell_y,
                    double path_m, double margin_m, double ratio,
                    unsigned pass_index) noexcept
{
  const unsigned half_grid = std::max(width, height) / 2 + 1;
  const double cell = std::min(cell_x, cell_y);
  const double half_ld_m =
    0.5 * std::max(0.0, margin_m) * std::max(ratio, 1.0);
  const double reach_m = std::max(0.0, path_m) + half_ld_m;
  unsigned reach_cells = 0;
  if (cell > 0 && reach_m > 0)
    reach_cells = unsigned(std::ceil(reach_m / cell));

  unsigned first = reach_cells > 0 ? reach_cells : half_grid;
  if (first < 1)
    first = 1;
  if (first > half_grid)
    first = half_grid;

  LogFmt("GlideCone options: first_check pass={} path={:.0f}m "
         "margin={:.0f}m half_ld={:.0f}m reach={:.0f}m cell={:.0f}m "
         "cells={} half_grid={} using={} step={}",
         pass_index, path_m, margin_m, half_ld_m, reach_m, cell,
         reach_cells, half_grid, first, std::max(1u, first / 2));
  return first;
}

bool
RunPass(const GlideConeDownwardPass &pass, unsigned width, unsigned height,
        double cell_x, double cell_y, float max_alt,
        double path_distance_m, double margin_m, unsigned iteration_cap,
        const std::function<bool()> &should_abort,
        std::vector<float> *arrival_out,
        std::vector<int> *origin_out,
        std::vector<std::uint8_t> &mask,
        unsigned &iterations, bool &hit_cap,
        [[maybe_unused]] unsigned pass_index) noexcept
{
  const std::size_t count = std::size_t(width) * height;
  if (pass.floors.size() != count || pass.ratio <= 0 ||
      pass.gi < 0 || pass.gj < 0 ||
      unsigned(pass.gi) >= width || unsigned(pass.gj) >= height)
    return false;

  const auto t_pass = Clock::now();
#if GLIDECONE_TIMING
  /* Instrumentation only (GlideConeTiming.hpp); microseconds. */
  const std::uint64_t tp_pass = GT::NowUs();
  std::uint64_t tp = tp_pass;
  std::uint64_t build_us = 0;
  std::uint64_t up_floors_us = 0, up_cells_us = 0, up_copy_us = 0;
  std::uint64_t setup_us = 0;
  /* CPU time spent enqueuing propagate dispatches (glUseProgram,
     glBindBufferBase x3, glDispatchCompute, glMemoryBarrier). */
  std::uint64_t enqueue_us = 0;
  std::uint64_t flush_us = 0;
  unsigned flushes = 0;
  std::uint64_t abort_poll_us = 0;
  GT::Stat chk_reset, chk_sum, chk_copy, chk_wait, chk_map, chk_total;
  std::uint64_t first_wait_us = 0;
  std::string chk_list; /* iteration:wait_ms/changed_cells */
  std::uint64_t rb_copy_us = 0, rb_wait_us = 0, rb_map_us = 0;
  std::uint64_t rb_cpu_us = 0, rb_unmap_us = 0;
#if GLIDECONE_TIMING_ISOLATE
  std::uint64_t iso_upload_us = 0, iso_tail_us = 0;
  GT::Stat iso_prop;
#endif
#if GLIDECONE_TIMING_GPU
  GT::GpuTimerSet gpu_timer;
#endif
#endif
  const std::size_t start = std::size_t(pass.gj) * width + pass.gi;
  std::vector<GpuCell> cells(count);
  for (std::size_t i = 0; i < count; ++i) {
    cells[i].alt = -1.f;
    cells[i].ox = -1;
    cells[i].oy = -1;
    cells[i].flags = 0;
  }
  const float floor = pass.floors[start];
  /* Seed only where the upward cone has a floor and arrival clears it. */
  if (floor < max_alt && pass.start_alt > floor) {
    cells[start].alt = pass.start_alt;
    cells[start].ox = pass.gi;
    cells[start].oy = pass.gj;
    cells[start].flags = FLAG_OPTION | FLAG_CHANGED;
  }

  const auto t_upload = Clock::now();
  GLIDECONE_TIMING_ONLY(build_us = GT::SinceUs(tp_pass); tp = GT::NowUs();)
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.floors);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  GLsizeiptr(count * sizeof(float)), pass.floors.data());
  GLIDECONE_TIMING_ONLY(up_floors_us = GT::SinceUs(tp); tp = GT::NowUs();)
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.cell_a);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, pool.cell_bytes, cells.data());
  GLIDECONE_TIMING_ONLY(up_cells_us = GT::SinceUs(tp); tp = GT::NowUs();)
  glBindBuffer(GL_COPY_READ_BUFFER, pool.cell_a);
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.cell_b);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                      0, 0, pool.cell_bytes);
  GLIDECONE_TIMING_ONLY(up_copy_us = GT::SinceUs(tp);)
  const unsigned upload_ms = ElapsedMs(t_upload);

#if GLIDECONE_TIMING && GLIDECONE_TIMING_ISOLATE
  /* EXTRA SYNC (debug only): make the GPU finish the uploads so their
     cost is not charged to the first convergence check. */
  tp = GT::NowUs();
  WaitGpuFence();
  iso_upload_us = GT::SinceUs(tp);
#endif

  GLIDECONE_TIMING_ONLY(tp = GT::NowUs();)
  glUseProgram(pool.program);
  glUniform1i(pool.loc_width, int(width));
  glUniform1i(pool.loc_height, int(height));
  glUniform1f(pool.loc_cell_x, float(cell_x));
  glUniform1f(pool.loc_cell_y, float(cell_y));
  glUniform1f(pool.loc_ratio, float(pass.ratio));
  glUniform1f(pool.loc_max_alt, max_alt);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool.floors);
  GLIDECONE_TIMING_ONLY(setup_us = GT::SinceUs(tp);)

  GLuint cur = pool.cell_a;
  GLuint next = pool.cell_b;
  const unsigned wg_x = (width + 7) / 8;
  const unsigned wg_y = (height + 7) / 8;
  const unsigned max_iterations = iteration_cap > 0 ? iteration_cap : 2000u;
  unsigned first_check_at =
    OptionsFirstCheckAt(width, height, cell_x, cell_y,
                        path_distance_m, margin_m, pass.ratio,
                        pass_index);
  if (first_check_at > max_iterations)
    first_check_at = max_iterations;
  /* If still noisy after the first check, wait half that reach again. */
  const unsigned check_step = std::max(1u, first_check_at / 2);
  iterations = 0;
  hit_cap = false;
  bool converged = false;
  unsigned checks = 0;
  unsigned check_wait_ms = 0;

  const auto t_loop = Clock::now();
  GLIDECONE_TIMING_ONLY(const std::uint64_t tp_loop = GT::NowUs();)
  for (unsigned iter = 0; iter < max_iterations; ++iter) {
    /* no-op while a propagate query is already open */
    GLIDECONE_GPU_TIMER(gpu_timer.Begin(GPU_PROPAGATE);)
    GLIDECONE_TIMING_ONLY(tp = GT::NowUs();)
    if (should_abort && should_abort())
      return false;
    GLIDECONE_TIMING_ONLY(abort_poll_us += GT::SinceUs(tp);)
    ++iterations;

    GLIDECONE_TIMING_ONLY(tp = GT::NowUs();)
    glUseProgram(pool.program);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool.floors);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cur);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, next);
    glDispatchCompute(wg_x, wg_y, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    GLIDECONE_TIMING_ONLY(enqueue_us += GT::SinceUs(tp);)
    std::swap(cur, next);

    const bool check = iterations >= first_check_at &&
      ((iterations - first_check_at) % check_step == 0 ||
       iterations == max_iterations);
    if (!check) {
      if (iterations % FLUSH_EVERY == 0) {
        GLIDECONE_TIMING_ONLY(tp = GT::NowUs();)
        glFlush();
        GLIDECONE_TIMING_ONLY(flush_us += GT::SinceUs(tp); ++flushes;)
      }
      continue;
    }

    ++checks;
    GLIDECONE_GPU_TIMER(gpu_timer.End();)
#if GLIDECONE_TIMING && GLIDECONE_TIMING_ISOLATE
    /* EXTRA SYNC (debug only): drain the propagate dispatches so the
       check timers below only see the sum shader + readback. */
    tp = GT::NowUs();
    WaitGpuFence();
    iso_prop.Add(GT::SinceUs(tp));
#endif
    const auto t_check = Clock::now();
    GLIDECONE_TIMING_ONLY(const std::uint64_t tp_check = GT::NowUs();
                          tp = tp_check;)
    const std::uint32_t zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.change);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zero), &zero);
    GLIDECONE_TIMING_ONLY(const std::uint64_t reset_us = GT::SinceUs(tp);
                          tp = GT::NowUs();)

    GLIDECONE_GPU_TIMER(gpu_timer.Begin(GPU_SUM);)
    glUseProgram(pool.sum_program);
    glUniform1i(pool.sum_loc_width, int(width));
    glUniform1i(pool.sum_loc_height, int(height));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cur);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pool.change);
    glDispatchCompute(wg_x, wg_y, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_BUFFER_UPDATE_BARRIER_BIT);
    GLIDECONE_GPU_TIMER(gpu_timer.End();)
    GLIDECONE_TIMING_ONLY(const std::uint64_t sum_us = GT::SinceUs(tp);)

    std::uint32_t changes = 1;
    if (!ReadChangeCount(changes))
      changes = 1;
    check_wait_ms += ElapsedMs(t_check);
#if GLIDECONE_TIMING
    if (chk_wait.n == 0)
      first_wait_us = last_read_change.wait_us;
    chk_reset.Add(reset_us);
    chk_sum.Add(sum_us);
    chk_copy.Add(last_read_change.copy_us);
    chk_wait.Add(last_read_change.wait_us);
    chk_map.Add(last_read_change.map_us);
    chk_total.Add(GT::SinceUs(tp_check));
    GT::AppendSample(chk_list, iterations,
                     GT::Ms(last_read_change.wait_us), changes);
#endif
    if (changes == 0) {
      converged = true;
      break;
    }
  }
  hit_cap = !converged;
  if (hit_cap)
    LogFmt("GlideCone options: gpu pass={} hit iteration cap {} "
           "iter={}",
           pass_index, max_iterations, iterations);
  const unsigned loop_ms = ElapsedMs(t_loop);
  GLIDECONE_TIMING_ONLY(const std::uint64_t loop_us = GT::SinceUs(tp_loop);)
  GLIDECONE_GPU_TIMER(gpu_timer.End();)

#if GLIDECONE_TIMING && GLIDECONE_TIMING_ISOLATE
  /* EXTRA SYNC (debug only): drain trailing propagate dispatches (only
     non-zero when the loop hit max_iterations without converging). */
  tp = GT::NowUs();
  WaitGpuFence();
  iso_tail_us = GT::SinceUs(tp);
#endif

  const auto t_read = Clock::now();
  GLIDECONE_TIMING_ONLY(tp = GT::NowUs();)
  glBindBuffer(GL_COPY_READ_BUFFER, cur);
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.cell_read);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                      0, 0, pool.cell_bytes);
  GLIDECONE_TIMING_ONLY(rb_copy_us = GT::SinceUs(tp); tp = GT::NowUs();)
  WaitGpuFence();
  GLIDECONE_TIMING_ONLY(rb_wait_us = GT::SinceUs(tp); tp = GT::NowUs();)
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.cell_read);
  const auto *mapped = static_cast<const GpuCell *>(
    glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, pool.cell_bytes,
                     GL_MAP_READ_BIT));
  GLIDECONE_TIMING_ONLY(rb_map_us = GT::SinceUs(tp); tp = GT::NowUs();)
  if (mapped == nullptr)
    return false;

  mask.assign(count, 0);
  std::vector<float> arrival(count, -1.f);
  std::vector<int> origin(count, -1);
  for (std::size_t i = 0; i < count; ++i) {
    arrival[i] = mapped[i].alt;
    if (mapped[i].flags & FLAG_OPTION)
      mask[i] = 1;
    if (mapped[i].ox >= 0 && mapped[i].oy >= 0 &&
        unsigned(mapped[i].ox) < width && unsigned(mapped[i].oy) < height)
      origin[i] = mapped[i].oy * int(width) + mapped[i].ox;
  }
  GLIDECONE_TIMING_ONLY(rb_cpu_us = GT::SinceUs(tp); tp = GT::NowUs();)
  glUnmapBuffer(GL_COPY_WRITE_BUFFER);
  GLIDECONE_TIMING_ONLY(rb_unmap_us = GT::SinceUs(tp);)
  const unsigned read_ms = ElapsedMs(t_read);
  /* Mask is the OPTION bit from readback — no separate pass. */
  constexpr unsigned mask_ms = 0;
  GLIDECONE_TIMING_ONLY(constexpr std::uint64_t mask_us = 0;)

#if GLIDECONE_TIMING && GLIDECONE_TIMING_GPU
  /* The readback fence above already covered every query, so this does
     not stall (unready queries are reported as pending). */
  const auto gpu = gpu_timer.Collect();
#endif

  if (arrival_out != nullptr)
    *arrival_out = std::move(arrival);
  if (origin_out != nullptr)
    *origin_out = std::move(origin);

#if GLIDECONE_TIMING
  const std::uint64_t total_us = GT::SinceUs(tp_pass);
  /* Same fields as the uninstrumented line, plus the pass index. */
  LogFmt("GlideCone options: gpu pass={} ratio={:.2f} iter={} checks={} "
         "upload={}ms loop={}ms checkwait={}ms read={}ms mask={}ms "
         "total={}ms",
         pass_index, pass.ratio, iterations, checks, upload_ms, loop_ms,
         check_wait_ms, read_ms, mask_ms, ElapsedMs(t_pass));
  const std::uint64_t upload_us = up_floors_us + up_cells_us + up_copy_us;
  LogFmt("GlideCone options: gpu pass={} cpu_ms build={:.3f} "
         "upload={:.3f} (floors={:.3f} cells={:.3f} copy={:.3f}) "
         "setup={:.3f} loop={:.3f} enqueue={:.3f} (iter={} avg={:.3f}us) "
         "flush={:.3f} (n={}) abort_poll={:.3f} mask={:.3f} total={:.3f}",
         pass_index, GT::Ms(build_us), GT::Ms(upload_us),
         GT::Ms(up_floors_us), GT::Ms(up_cells_us), GT::Ms(up_copy_us),
         GT::Ms(setup_us), GT::Ms(loop_us), GT::Ms(enqueue_us), iterations,
         iterations > 0 ? double(enqueue_us) / iterations : 0.,
         GT::Ms(flush_us), flushes, GT::Ms(abort_poll_us), GT::Ms(mask_us),
         GT::Ms(total_us));
  LogFmt("GlideCone options: gpu pass={} check_ms n={} first_at={} "
         "every={} reset={:.3f} sum_enqueue={:.3f} copy={:.3f} "
         "wait={:.3f} (min={:.3f} avg={:.3f} max={:.3f}) map={:.3f} "
         "total={:.3f} (min={:.3f} avg={:.3f} max={:.3f})",
         pass_index, chk_total.n, first_check_at, check_step,
         chk_reset.SumMs(), chk_sum.SumMs(), chk_copy.SumMs(),
         chk_wait.SumMs(), chk_wait.MinMs(), chk_wait.AvgMs(),
         chk_wait.MaxMs(), chk_map.SumMs(), chk_total.SumMs(),
         chk_total.MinMs(), chk_total.AvgMs(), chk_total.MaxMs());
  LogFmt("GlideCone options: gpu pass={} check_list iter:wait_ms/changed=[{}]",
         pass_index, chk_list);
  LogFmt("GlideCone options: gpu pass={} read_ms copy={:.3f} wait={:.3f} "
         "map={:.3f} cpu_copy={:.3f} unmap={:.3f} total={:.3f}",
         pass_index, GT::Ms(rb_copy_us), GT::Ms(rb_wait_us),
         GT::Ms(rb_map_us), GT::Ms(rb_cpu_us), GT::Ms(rb_unmap_us),
         GT::Ms(rb_copy_us + rb_wait_us + rb_map_us + rb_cpu_us +
                rb_unmap_us));
  /* CPU-side estimate of GPU work, valid with or without timer queries:
     first_wait ~ GPU time of the first first_check_at iterations (they
     are only flushed, never waited for, before the first check);
     blocking = all CPU time blocked in fences; span = loop start to
     readback fence complete (upper bound of GPU busy time). */
  LogFmt("GlideCone options: gpu pass={} gpu_est_ms first_wait={:.3f} "
         "blocking={:.3f} span={:.3f}",
         pass_index, GT::Ms(first_wait_us),
         GT::Ms(chk_wait.sum + rb_wait_us),
         GT::Ms(loop_us + rb_copy_us + rb_wait_us));
#if GLIDECONE_TIMING_GPU
  if (gpu_timer.IsEnabled())
    LogFmt("GlideCone options: gpu pass={} gpu_timer=ok{} "
           "propagate={:.3f}ms sum={:.3f}ms queries={} pending={} "
           "disjoint={}",
           pass_index,
           GT::GetGpuTimerSupport().get_ui64 != nullptr ? "" : "(32bit)",
           gpu.MsOf(GPU_PROPAGATE), gpu.MsOf(GPU_SUM), gpu.ready,
           gpu.pending, gpu.disjoint);
  else
    LogFmt("GlideCone options: gpu pass={} gpu_timer=unavailable",
           pass_index);
#else
  LogFmt("GlideCone options: gpu pass={} gpu_timer=disabled", pass_index);
#endif
#if GLIDECONE_TIMING_ISOLATE
  LogFmt("GlideCone options: gpu pass={} ISOLATE (extra syncs, timings "
         "perturbed) upload_gpu={:.3f} propagate_gpu={:.3f} "
         "(n={} max={:.3f}) tail={:.3f}",
         pass_index, GT::Ms(iso_upload_us), iso_prop.SumMs(), iso_prop.n,
         iso_prop.MaxMs(), GT::Ms(iso_tail_us));
#endif
#else
  LogFmt("GlideCone options: gpu pass ratio={:.2f} iter={} checks={} "
         "upload={}ms loop={}ms checkwait={}ms read={}ms mask={}ms "
         "total={}ms",
         pass.ratio, iterations, checks, upload_ms, loop_ms,
         check_wait_ms, read_ms, mask_ms, ElapsedMs(t_pass));
#endif
  return true;
}

} // namespace

bool
RunGlideConeDownward(const GlideConeDownwardJob &job,
                     const std::function<bool()> &should_abort,
                     GlideConeDownwardReady &out) noexcept
{
  out = {};
  out.generation = job.generation;
  out.display = job.display;
  out.bounds = job.bounds;
  out.start_location = job.start_location;
  out.aircraft_gi = job.aircraft_gi;
  out.aircraft_gj = job.aircraft_gj;
  out.width = job.width;
  out.height = job.height;

  const auto t0 = Clock::now();
  GLIDECONE_TIMING_ONLY(const std::uint64_t t_run = GT::NowUs();)
  LogFmt("GlideCone options: gpu run {}x{} passes={}",
         job.width, job.height, job.passes.size());

  if (job.passes.empty() || job.width == 0 || job.height == 0)
    return false;

  const std::size_t count = std::size_t(job.width) * job.height;
  GLIDECONE_TIMING_ONLY(std::uint64_t tp = GT::NowUs();)
  if (!EnsurePool(count)) {
    LogFmt("GlideCone options: gpu pool failed");
    return false;
  }
#if GLIDECONE_TIMING
  const std::uint64_t pool_us = GT::SinceUs(tp);
  /* per pass: RunPass wall time, and CPU gap since the previous pass
     returned (gap[0] = since EnsurePool) */
  std::uint64_t pass_us[3] = {};
  std::uint64_t gap_us[3] = {};
  unsigned passes_run = 0;
  std::uint64_t prev_end = GT::NowUs();
#endif

  unsigned iterations = 0;
  bool hit_cap = false;
  GLIDECONE_TIMING_ONLY(tp = GT::NowUs(); gap_us[0] = tp - prev_end;)
  if (!RunPass(job.passes[0], job.width, job.height, job.cell_x, job.cell_y,
               job.max_alt, job.path_distance_m, job.margin_m,
               job.iteration_cap, should_abort, &out.arrival, &out.origin,
               out.mask, iterations, hit_cap, 0)) {
    LogFmt("GlideCone options: gpu pass failed");
    return false;
  }
  GLIDECONE_TIMING_ONLY(prev_end = GT::NowUs(); pass_us[0] = prev_end - tp;
                        passes_run = 1;)

  out.iterations = iterations;
  out.hit_iteration_cap = hit_cap;
  out.floors = job.passes[0].floors;
  out.start_index = job.passes[0].gj * int(job.width) + job.passes[0].gi;
  out.ok = true;

  if (job.passes.size() > 1) {
    unsigned ignored = 0;
    bool pass_hit = false;
    GLIDECONE_TIMING_ONLY(tp = GT::NowUs(); gap_us[1] = tp - prev_end;)
    if (!RunPass(job.passes[1], job.width, job.height, job.cell_x, job.cell_y,
                 job.max_alt, job.path_distance_m, job.margin_m,
                 job.iteration_cap, should_abort, nullptr, nullptr,
                 out.mask10, ignored, pass_hit, 1)) {
      if (should_abort && should_abort())
        return false;
      out.mask10.clear();
    } else if (pass_hit)
      out.hit_iteration_cap = true;
    GLIDECONE_TIMING_ONLY(prev_end = GT::NowUs(); pass_us[1] = prev_end - tp;
                          passes_run = 2;)
  }
  if (job.passes.size() > 2) {
    unsigned ignored = 0;
    bool pass_hit = false;
    GLIDECONE_TIMING_ONLY(tp = GT::NowUs(); gap_us[2] = tp - prev_end;)
    if (!RunPass(job.passes[2], job.width, job.height, job.cell_x, job.cell_y,
                 job.max_alt, job.path_distance_m, job.margin_m,
                 job.iteration_cap, should_abort, nullptr, nullptr,
                 out.mask20, ignored, pass_hit, 2)) {
      if (should_abort && should_abort())
        return false;
      out.mask20.clear();
    } else if (pass_hit)
      out.hit_iteration_cap = true;
    GLIDECONE_TIMING_ONLY(prev_end = GT::NowUs(); pass_us[2] = prev_end - tp;
                          passes_run = 3;)
  }
  unsigned mask_n = 0;
  for (const std::uint8_t cell : out.mask)
    if (cell != 0)
      ++mask_n;
#if GLIDECONE_TIMING
  LogFmt("GlideCone options: gpu run timing passes={} ensure_pool={:.3f}ms "
         "(programs={:.3f} realloc={}) pass_ms=[{:.3f},{:.3f},{:.3f}] "
         "sum_passes={:.3f}ms gaps_ms=[{:.3f},{:.3f},{:.3f}] "
         "wall={:.3f}ms",
         passes_run, GT::Ms(pool_us), GT::Ms(last_ensure_programs_us),
         last_pool_realloc, GT::Ms(pass_us[0]), GT::Ms(pass_us[1]),
         GT::Ms(pass_us[2]),
         GT::Ms(pass_us[0] + pass_us[1] + pass_us[2]),
         GT::Ms(gap_us[0]), GT::Ms(gap_us[1]), GT::Ms(gap_us[2]),
         GT::Ms(GT::SinceUs(t_run)));
#endif
  LogFmt("GlideCone options: gpu run done mask={} iter={} {}ms ok={}",
         mask_n, out.iterations, ElapsedMs(t0), out.ok);
  return out.ok;
}

void
DestroyGlideConeDownward() noexcept
{
  if (pool.program != 0)
    glDeleteProgram(pool.program);
  pool.program = 0;
  if (pool.sum_program != 0)
    glDeleteProgram(pool.sum_program);
  pool.sum_program = 0;
  DeleteBuffers();
}

#endif
