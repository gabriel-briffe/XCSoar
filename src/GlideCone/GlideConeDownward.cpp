// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeDownward.hpp"
#include "LogFile.hpp"

#ifdef HAVE_GLES_COMPUTE

#include "ui/opengl/GLESCompute.hpp"

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

constexpr std::uint32_t FLAG_GROUND = 1u;
constexpr std::uint32_t FLAG_CHANGED = 2u;

/**
 * After the wavefront can possibly have quieted, read the change
 * counter this often (same stride as the upward cone batch).  First
 * check is at half the Manhattan diameter, not a fixed 300.
 */
constexpr unsigned CHECK_EVERY = 64;

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
 * GLES 3.1 port of gpu-MC DOWNWARD_PROPAGATE_SHADER.
 * No atomics here — change counting is a separate sum pass (gpu-MC
 * CHANGED_SUM_SHADER), run only on convergence-check iterations.
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

const uint FLAG_GROUND = 1u;
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

bool isGcAt(int x, int y) {
  if (!inBounds(x, y))
    return false;
  return (cin[idx(x, y)].flags & FLAG_GROUND) != 0u;
}

bool isGcCell(uint flags) { return (flags & FLAG_GROUND) != 0u; }
bool wasModified(uint flags) { return (flags & FLAG_CHANGED) != 0u; }

uint packFlags(bool gc, bool changed) {
  uint f = 0u;
  if (gc) f = f | FLAG_GROUND;
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

bool neighborIsActiveAir(int nx, int ny, int myOx, int myOy) {
  if (!inBounds(nx, ny))
    return false;
  uint nflags = cin[idx(nx, ny)].flags;
  if (isGcCell(nflags) || !wasModified(nflags))
    return false;
  int nox = cin[idx(nx, ny)].ox;
  int noy = cin[idx(nx, ny)].oy;
  return nox != myOx || noy != myOy;
}

void consider(int nx, int ny, int x, int y, int myOx, int myOy,
              inout float bestArrival, inout int bestOx, inout int bestOy) {
  if (!neighborIsActiveAir(nx, ny, myOx, myOy))
    return;
  int electedX = nx;
  int electedY = ny;
  int pox = cin[idx(nx, ny)].ox;
  int poy = cin[idx(nx, ny)].oy;
  if (isInViewToOrigin(x, y, pox, poy)) {
    electedX = pox;
    electedY = poy;
  }
  if (!originValid(electedX, electedY) || isGcAt(electedX, electedY))
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
  cout[i].flags = flags & FLAG_GROUND;
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

  if (isGcCell(curFlags)) {
    passthrough(i, myOx, myOy, curAlt, curFlags);
    cout[i].flags = FLAG_GROUND;
    return;
  }

  bool hasNeighbor = neighborIsActiveAir(x - 1, y - 1, myOx, myOy)
    || neighborIsActiveAir(x, y - 1, myOx, myOy)
    || neighborIsActiveAir(x + 1, y - 1, myOx, myOy)
    || neighborIsActiveAir(x - 1, y, myOx, myOy)
    || neighborIsActiveAir(x + 1, y, myOx, myOy)
    || neighborIsActiveAir(x - 1, y + 1, myOx, myOy)
    || neighborIsActiveAir(x, y + 1, myOx, myOy)
    || neighborIsActiveAir(x + 1, y + 1, myOx, myOy);
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

  float newAlt = bestArrival;
  bool newGc = false;
  if (hasConeFloor(i) && bestArrival < floors[i]) {
    newAlt = floors[i];
    newGc = true;
  }
  cout[i].alt = newAlt;
  cout[i].ox = bestOx;
  cout[i].oy = bestOy;
  bool changed = bestOx != myOx || bestOy != myOy
    || abs(newAlt - curAlt) > 0.001 || newGc;
  cout[i].flags = packFlags(newGc, changed);
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
  if (!EnsurePrograms())
    return false;

  if (pool.count == count && pool.floors != 0)
    return true;

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

void
MaskFromArrivals(const std::vector<float> &arrival,
                 const std::vector<float> &floors, float max_alt,
                 std::vector<std::uint8_t> &mask) noexcept
{
  mask.assign(arrival.size(), 0);
  for (std::size_t i = 0; i < arrival.size() && i < floors.size(); ++i) {
    if (!(floors[i] < max_alt))
      continue;
    if (arrival[i] > floors[i])
      mask[i] = 1;
  }
}

/**
 * Read the change counter via a staging copy (gpu-MC changeReadBuffer).
 * Returns false if the map fails (caller should keep iterating).
 */
bool
ReadChangeCount(std::uint32_t &changes) noexcept
{
  glBindBuffer(GL_COPY_READ_BUFFER, pool.change);
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.change_read);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                      0, 0, sizeof(changes));
  WaitGpuFence();
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.change_read);
  const auto *mapped = static_cast<const std::uint32_t *>(
    glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, sizeof(changes),
                     GL_MAP_READ_BIT));
  if (mapped == nullptr)
    return false;
  changes = *mapped;
  glUnmapBuffer(GL_COPY_WRITE_BUFFER);
  return true;
}

bool
RunPass(const GlideConeDownwardPass &pass, unsigned width, unsigned height,
        double cell_x, double cell_y, float max_alt,
        const std::function<bool()> &should_abort,
        std::vector<float> *arrival_out,
        std::vector<int> *origin_out,
        std::vector<std::uint8_t> &mask,
        unsigned &iterations) noexcept
{
  const std::size_t count = std::size_t(width) * height;
  if (pass.floors.size() != count || pass.ratio <= 0 ||
      pass.gi < 0 || pass.gj < 0 ||
      unsigned(pass.gi) >= width || unsigned(pass.gj) >= height)
    return false;

  const auto t_pass = Clock::now();
  const std::size_t start = std::size_t(pass.gj) * width + pass.gi;
  std::vector<GpuCell> cells(count);
  for (std::size_t i = 0; i < count; ++i) {
    cells[i].alt = -1.f;
    cells[i].ox = -1;
    cells[i].oy = -1;
    cells[i].flags = 0;
  }
  const float floor = pass.floors[start];
  if (floor < max_alt && pass.start_alt < floor) {
    cells[start].alt = floor;
    cells[start].flags = FLAG_GROUND;
  } else {
    cells[start].alt = pass.start_alt;
    cells[start].flags = FLAG_CHANGED;
  }
  cells[start].ox = pass.gi;
  cells[start].oy = pass.gj;

  const auto t_upload = Clock::now();
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.floors);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  GLsizeiptr(count * sizeof(float)), pass.floors.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.cell_a);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, pool.cell_bytes, cells.data());
  glBindBuffer(GL_COPY_READ_BUFFER, pool.cell_a);
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.cell_b);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                      0, 0, pool.cell_bytes);
  const unsigned upload_ms = ElapsedMs(t_upload);

  glUseProgram(pool.program);
  glUniform1i(pool.loc_width, int(width));
  glUniform1i(pool.loc_height, int(height));
  glUniform1f(pool.loc_cell_x, float(cell_x));
  glUniform1f(pool.loc_cell_y, float(cell_y));
  glUniform1f(pool.loc_ratio, float(pass.ratio));
  glUniform1f(pool.loc_max_alt, max_alt);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool.floors);

  GLuint cur = pool.cell_a;
  GLuint next = pool.cell_b;
  const unsigned wg_x = (width + 7) / 8;
  const unsigned wg_y = (height + 7) / 8;
  const unsigned max_iterations = width + height;
  /* Same early-skip idea as the upward cone: a wavefront cannot have
     quieted before covering roughly half the Manhattan diameter. */
  const unsigned first_check_at = std::max(width, height) / 2 + 1;
  iterations = 0;
  unsigned checks = 0;
  unsigned check_wait_ms = 0;

  const auto t_loop = Clock::now();
  for (unsigned iter = 0; iter < max_iterations; ++iter) {
    if (should_abort && should_abort())
      return false;
    ++iterations;

    glUseProgram(pool.program);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool.floors);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cur);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, next);
    glDispatchCompute(wg_x, wg_y, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    std::swap(cur, next);

    const bool check = iterations >= first_check_at &&
      (iterations - first_check_at) % CHECK_EVERY == 0;
    if (!check) {
      if (iterations % FLUSH_EVERY == 0)
        glFlush();
      continue;
    }

    ++checks;
    const auto t_check = Clock::now();
    const std::uint32_t zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool.change);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zero), &zero);

    glUseProgram(pool.sum_program);
    glUniform1i(pool.sum_loc_width, int(width));
    glUniform1i(pool.sum_loc_height, int(height));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cur);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pool.change);
    glDispatchCompute(wg_x, wg_y, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_BUFFER_UPDATE_BARRIER_BIT);

    std::uint32_t changes = 1;
    if (!ReadChangeCount(changes))
      changes = 1;
    check_wait_ms += ElapsedMs(t_check);
    if (changes == 0)
      break;
  }
  const unsigned loop_ms = ElapsedMs(t_loop);

  const auto t_read = Clock::now();
  glBindBuffer(GL_COPY_READ_BUFFER, cur);
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.cell_read);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                      0, 0, pool.cell_bytes);
  WaitGpuFence();
  glBindBuffer(GL_COPY_WRITE_BUFFER, pool.cell_read);
  const auto *mapped = static_cast<const GpuCell *>(
    glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, pool.cell_bytes,
                     GL_MAP_READ_BIT));
  if (mapped == nullptr)
    return false;

  std::vector<float> arrival(count, -1.f);
  std::vector<int> origin(count, -1);
  for (std::size_t i = 0; i < count; ++i) {
    arrival[i] = mapped[i].alt;
    if (mapped[i].ox >= 0 && mapped[i].oy >= 0 &&
        unsigned(mapped[i].ox) < width && unsigned(mapped[i].oy) < height)
      origin[i] = mapped[i].oy * int(width) + mapped[i].ox;
  }
  glUnmapBuffer(GL_COPY_WRITE_BUFFER);
  const unsigned read_ms = ElapsedMs(t_read);

  const auto t_mask = Clock::now();
  MaskFromArrivals(arrival, pass.floors, max_alt, mask);
  const unsigned mask_ms = ElapsedMs(t_mask);

  if (arrival_out != nullptr)
    *arrival_out = std::move(arrival);
  if (origin_out != nullptr)
    *origin_out = std::move(origin);

  LogFmt("GlideCone options: gpu pass ratio={:.2f} iter={} checks={} "
         "upload={}ms loop={}ms checkwait={}ms read={}ms mask={}ms "
         "total={}ms",
         pass.ratio, iterations, checks, upload_ms, loop_ms,
         check_wait_ms, read_ms, mask_ms, ElapsedMs(t_pass));
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
  LogFmt("GlideCone options: gpu run {}x{} passes={}",
         job.width, job.height, job.passes.size());

  if (job.passes.empty() || job.width == 0 || job.height == 0)
    return false;

  const std::size_t count = std::size_t(job.width) * job.height;
  if (!EnsurePool(count)) {
    LogFmt("GlideCone options: gpu pool failed");
    return false;
  }

  unsigned iterations = 0;
  if (!RunPass(job.passes[0], job.width, job.height, job.cell_x, job.cell_y,
               job.max_alt, should_abort, &out.arrival, &out.origin,
               out.mask, iterations)) {
    LogFmt("GlideCone options: gpu pass failed");
    return false;
  }

  out.iterations = iterations;
  out.floors = job.passes[0].floors;
  out.start_index = job.passes[0].gj * int(job.width) + job.passes[0].gi;
  out.ok = true;

  if (job.passes.size() > 1) {
    unsigned ignored = 0;
    if (!RunPass(job.passes[1], job.width, job.height, job.cell_x, job.cell_y,
                 job.max_alt, should_abort, nullptr, nullptr,
                 out.mask10, ignored)) {
      if (should_abort && should_abort())
        return false;
      out.mask10.clear();
    }
  }
  if (job.passes.size() > 2) {
    unsigned ignored = 0;
    if (!RunPass(job.passes[2], job.width, job.height, job.cell_x, job.cell_y,
                 job.max_alt, should_abort, nullptr, nullptr,
                 out.mask20, ignored)) {
      if (should_abort && should_abort())
        return false;
      out.mask20.clear();
    }
  }
  unsigned mask_n = 0;
  for (const std::uint8_t cell : out.mask)
    if (cell != 0)
      ++mask_n;
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
