// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeCompute.hpp"

#ifdef HAVE_GLES_COMPUTE

#include "LogFile.hpp"

#include <GLES3/gl31.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

/**
 * One grid cell as stored in the GPU SSBO.  The layout matches the
 * std430 `Cell` struct in the compute shader (16 bytes, no padding).
 */
struct GpuCell {
  float alt;
  std::int32_t ox;
  std::int32_t oy;
  std::uint32_t flags;
};

static_assert(sizeof(GpuCell) == 16, "GpuCell must match std430 layout");

constexpr std::uint32_t FLAG_CHANGED = 2u;

/*
 * GLES 3.1 compute port of the WebGPU glide cone propagation.
 *
 * Ground clearance / line-of-sight blocking and the relay-origin
 * propagation follow the original single-mode algorithm.  The whole
 * per-cell state (altitude, origin, flags) is packed into one std430
 * struct so the shader needs only three shader-storage blocks (elevation
 * plus a ping-pong pair), staying within the GLES 3.1 guaranteed minimum.
 */
constexpr char PROPAGATE_SHADER[] = R"GLSL(#version 310 es
precision highp float;
precision highp int;

layout(local_size_x = 8, local_size_y = 8) in;

struct Cell {
  float alt;
  int ox;
  int oy;
  uint flags;
};

layout(std430, binding = 0) readonly buffer ElevBuf { float elev[]; };
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
const float ALT_EPSILON = 0.001;

int idx(int x, int y) { return y * uWidth + x; }

bool inBounds(int x, int y) {
  return x >= 0 && y >= 0 && x < uWidth && y < uHeight;
}

bool originValid(int ox, int oy) { return inBounds(ox, oy); }

bool hasStoredOrigin(int ox, int oy) {
  return originValid(ox, oy) && !(ox == -1 && oy == -1);
}

ivec2 originAt(int i) { return ivec2(cin[i].ox, cin[i].oy); }

bool isGroundAt(int x, int y) {
  if (!inBounds(x, y))
    return false;
  return (cin[idx(x, y)].flags & FLAG_GROUND) != 0u;
}

bool isGroundCell(uint flags) { return (flags & FLAG_GROUND) != 0u; }
bool wasModified(uint flags) { return (flags & FLAG_CHANGED) != 0u; }

uint packFlags(bool ground, bool changed) {
  uint f = 0u;
  if (ground) f = f | FLAG_GROUND;
  if (changed) f = f | FLAG_CHANGED;
  return f;
}

// Full Bresenham line-of-sight, blocked by ground cells.
bool isInViewToOrigin(int x0, int y0, int targetOx, int targetOy) {
  if (!originValid(targetOx, targetOy))
    return false;
  if (x0 == targetOx && y0 == targetOy)
    return true;
  int adx = abs(targetOx - x0);
  int ady = abs(targetOy - y0);
  int x1 = x0;
  int y1 = y0;
  int xstep = (targetOx > x1) ? 1 : -1;
  int ystep = (targetOy > y1) ? 1 : -1;
  int dx = adx;
  int dy = ady;
  int ddy = dy * 2;
  int ddx = dx * 2;
  int error = dx;
  int errorprev = error;

  if (dx >= dy) {
    for (int step = 0; step < dx; step = step + 1) {
      x1 = x1 + xstep;
      error = error + ddy;
      if (error > ddx) {
        y1 = y1 + ystep;
        error = error - ddx;
        if (error + errorprev < ddx) {
          if (isGroundAt(x1, y1 - ystep)) return false;
        } else if (error + errorprev > ddx) {
          if (isGroundAt(x1 - xstep, y1)) return false;
        }
      }
      if (!(x1 == targetOx && y1 == targetOy) && isGroundAt(x1, y1))
        return false;
      errorprev = error;
    }
  } else {
    for (int step = 0; step < dy; step = step + 1) {
      y1 = y1 + ystep;
      error = error + ddx;
      if (error > ddy) {
        x1 = x1 + xstep;
        error = error - ddy;
        if (error + errorprev < ddy) {
          if (isGroundAt(x1 - xstep, y1)) return false;
        } else if (error + errorprev > ddy) {
          if (isGroundAt(x1, y1 - ystep)) return false;
        }
      }
      if (!(x1 == targetOx && y1 == targetOy) && isGroundAt(x1, y1))
        return false;
      errorprev = error;
    }
  }
  return true;
}

float coneAlt(int ox, int oy, int x, int y) {
  int oi = idx(ox, oy);
  float dx = float(x - ox) * uCellSizeX;
  float dy = float(y - oy) * uCellSizeY;
  return cin[oi].alt + sqrt(dx * dx + dy * dy) / uGlideRatio;
}

ivec2 electedFromNeighbor(int x, int y, int px, int py) {
  if (isGroundAt(px, py))
    return ivec2(px, py);
  ivec2 parentOrigin = originAt(idx(px, py));
  if (isInViewToOrigin(x, y, parentOrigin.x, parentOrigin.y))
    return parentOrigin;
  return ivec2(px, py);
}

bool neighborIsModifiedAndDifferentOrigin(int nx, int ny, int myOx, int myOy) {
  if (!inBounds(nx, ny))
    return false;
  int ni = idx(nx, ny);
  if (!wasModified(cin[ni].flags))
    return false;
  ivec2 norigin = originAt(ni);
  return norigin.x != myOx || norigin.y != myOy;
}

const ivec2 NEIGHBOR_OFFSETS[8] = ivec2[8](
  ivec2(-1, -1), ivec2(0, -1), ivec2(1, -1),
  ivec2(-1, 0), ivec2(1, 0),
  ivec2(-1, 1), ivec2(0, 1), ivec2(1, 1));

bool hasActiveNeighbor(int x, int y, int myOx, int myOy) {
  for (int k = 0; k < 8; k = k + 1) {
    ivec2 off = NEIGHBOR_OFFSETS[k];
    if (neighborIsModifiedAndDifferentOrigin(x + off.x, y + off.y, myOx, myOy))
      return true;
  }
  return false;
}

vec3 tryModifiedNeighbor(int nx, int ny, int x, int y, int myOx, int myOy,
                         float bestReq, int bestOx, int bestOy) {
  if (!neighborIsModifiedAndDifferentOrigin(nx, ny, myOx, myOy))
    return vec3(bestReq, float(bestOx), float(bestOy));
  ivec2 elected = electedFromNeighbor(x, y, nx, ny);
  if (!originValid(elected.x, elected.y))
    return vec3(bestReq, float(bestOx), float(bestOy));
  float req = coneAlt(elected.x, elected.y, x, y);
  if (req < bestReq)
    return vec3(req, float(elected.x), float(elected.y));
  return vec3(bestReq, float(bestOx), float(bestOy));
}

void passthrough(int i, ivec2 curO, float curAlt, uint curFlags) {
  cout[i].alt = curAlt;
  cout[i].ox = curO.x;
  cout[i].oy = curO.y;
  cout[i].flags = curFlags & FLAG_GROUND;
}

void main() {
  int x = int(gl_GlobalInvocationID.x);
  int y = int(gl_GlobalInvocationID.y);
  if (!inBounds(x, y))
    return;

  int i = idx(x, y);
  ivec2 curO = originAt(i);
  float curAlt = cin[i].alt;
  uint curFlags = cin[i].flags;
  int myOx = curO.x;
  int myOy = curO.y;

  if (isGroundCell(curFlags)) {
    if (!hasActiveNeighbor(x, y, myOx, myOy)) {
      passthrough(i, curO, curAlt, curFlags);
      cout[i].flags = FLAG_GROUND;
      return;
    }

    float currentReq = uMaxAlt;
    if (hasStoredOrigin(myOx, myOy))
      currentReq = coneAlt(myOx, myOy, x, y);

    float bestReq = currentReq;
    int bestOx = myOx;
    int bestOy = myOy;

    for (int k = 0; k < 8; k = k + 1) {
      ivec2 off = NEIGHBOR_OFFSETS[k];
      vec3 pick = tryModifiedNeighbor(x + off.x, y + off.y, x, y, myOx, myOy,
                                      bestReq, bestOx, bestOy);
      bestReq = pick.x;
      bestOx = int(pick.y);
      bestOy = int(pick.z);
    }

    if (bestReq >= currentReq || bestReq >= uMaxAlt) {
      passthrough(i, curO, curAlt, curFlags);
      cout[i].flags = FLAG_GROUND;
      return;
    }

    cout[i].alt = curAlt;
    cout[i].ox = bestOx;
    cout[i].oy = bestOy;
    cout[i].flags = FLAG_GROUND;
    return;
  }

  if (!hasActiveNeighbor(x, y, myOx, myOy)) {
    passthrough(i, curO, curAlt, curFlags);
    return;
  }

  float bestReq = curAlt;
  int bestOx = myOx;
  int bestOy = myOy;

  for (int k = 0; k < 8; k = k + 1) {
    ivec2 off = NEIGHBOR_OFFSETS[k];
    vec3 pick = tryModifiedNeighbor(x + off.x, y + off.y, x, y, myOx, myOy,
                                    bestReq, bestOx, bestOy);
    bestReq = pick.x;
    bestOx = int(pick.y);
    bestOy = int(pick.z);
  }

  if (hasStoredOrigin(myOx, myOy) && bestReq >= curAlt) {
    passthrough(i, curO, curAlt, curFlags);
    return;
  }

  if (bestReq >= uMaxAlt) {
    passthrough(i, curO, curAlt, curFlags);
    return;
  }

  float newAlt = curAlt;
  int newOx = myOx;
  int newOy = myOy;
  bool newGround = isGroundCell(curFlags);

  if (bestReq <= elev[i]) {
    newAlt = elev[i];
    newOx = bestOx;
    newOy = bestOy;
    newGround = true;
  } else {
    newAlt = bestReq;
    newOx = bestOx;
    newOy = bestOy;
  }

  cout[i].alt = newAlt;
  cout[i].ox = newOx;
  cout[i].oy = newOy;

  bool changed = newOx != myOx
    || newOy != myOy
    || abs(newAlt - curAlt) > ALT_EPSILON
    || newGround != isGroundCell(curFlags);
  cout[i].flags = packFlags(newGround, changed);
}
)GLSL";

[[gnu::pure]]
bool
IsComputeContext() noexcept
{
  /* clear any pending error, then query the context version; on an ES2
     context GL_MAJOR_VERSION is an invalid enum and leaves major==0 */
  while (glGetError() != GL_NO_ERROR) {}

  GLint major = 0, minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  if (glGetError() != GL_NO_ERROR)
    return false;

  return major > 3 || (major == 3 && minor >= 1);
}

GLuint
CompileComputeProgram(const char *src) noexcept
{
  const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);

  GLint status = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    LogFormat("GlideCone compute compile failed: %s", log);
    glDeleteShader(shader);
    return 0;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, shader);
  glLinkProgram(program);
  glDeleteShader(shader);

  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    LogFormat("GlideCone compute link failed: %s", log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

} // anonymous namespace

void
GlideConeGpuSession::DeleteFence() noexcept
{
  if (fence != nullptr) {
    glDeleteSync(fence);
    fence = nullptr;
  }
}

void
GlideConeGpuSession::DeleteBuffers() noexcept
{
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
  glUseProgram(0);

  const GLuint bufs[3] = {elev_buf, cell_a, cell_b};
  if (elev_buf != 0 || cell_a != 0 || cell_b != 0)
    glDeleteBuffers(3, bufs);
  elev_buf = cell_a = cell_b = 0;
  cell_cur = cell_next = 0;
  cell_bytes = 0;
  width = height = wg_x = wg_y = 0;
}

void
GlideConeGpuSession::Cancel() noexcept
{
  if (!active && fence == nullptr && elev_buf == 0)
    return;

  DeleteFence();
  DeleteBuffers();
  active = false;
  remaining = 0;
}

bool
GlideConeGpuSession::EnsureProgram() noexcept
{
  if (program != 0)
    return true;
  program = CompileComputeProgram(PROPAGATE_SHADER);
  return program != 0;
}

bool
GlideConeGpuSession::Begin(const GlideConeGrid &grid) noexcept
{
  Cancel();

  if (!grid.IsValid() || !IsComputeContext())
    return false;
  if (!EnsureProgram())
    return false;

  width = grid.width;
  height = grid.height;
  const std::size_t count = std::size_t(width) * height;
  wg_x = (width + 7) / 8;
  wg_y = (height + 7) / 8;
  remaining = grid.iteration_cap > 0 ? grid.iteration_cap : 2000u;
  cell_bytes = GLsizeiptr(count * sizeof(GpuCell));

  std::vector<GpuCell> cells(count);
  for (std::size_t i = 0; i < count; ++i)
    cells[i] = GpuCell{grid.max_alt, -1, -1, 0u};

  for (const auto &s : grid.seeds) {
    if (s.x < 0 || s.y < 0 ||
        unsigned(s.x) >= width || unsigned(s.y) >= height)
      continue;
    const std::size_t seed = std::size_t(s.y) * width + s.x;
    cells[seed] = GpuCell{s.alt, s.x, s.y, FLAG_CHANGED};
  }

  GLuint bufs[3] = {};
  glGenBuffers(3, bufs);
  elev_buf = bufs[0];
  cell_a = bufs[1];
  cell_b = bufs[2];

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, elev_buf);
  glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(count * sizeof(float)),
               grid.elevation.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, cell_a);
  glBufferData(GL_SHADER_STORAGE_BUFFER, cell_bytes, cells.data(),
               GL_DYNAMIC_COPY);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, cell_b);
  glBufferData(GL_SHADER_STORAGE_BUFFER, cell_bytes, cells.data(),
               GL_DYNAMIC_COPY);

  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "uWidth"), int(width));
  glUniform1i(glGetUniformLocation(program, "uHeight"), int(height));
  glUniform1f(glGetUniformLocation(program, "uCellSizeX"),
              float(grid.cell_size_x_m));
  glUniform1f(glGetUniformLocation(program, "uCellSizeY"),
              float(grid.cell_size_y_m));
  glUniform1f(glGetUniformLocation(program, "uGlideRatio"),
              float(grid.glide_ratio));
  glUniform1f(glGetUniformLocation(program, "uMaxAlt"), grid.max_alt);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, elev_buf);
  cell_cur = cell_a;
  cell_next = cell_b;
  active = true;
  return true;
}

bool
GlideConeGpuSession::PollFence() noexcept
{
  if (fence == nullptr)
    return true;

  const GLenum r = glClientWaitSync(fence, 0, 0);
  if (r == GL_TIMEOUT_EXPIRED)
    return false;

  DeleteFence();
  if (r == GL_WAIT_FAILED) {
    /* drop the job; Draw() will keep the last-good field */
    Cancel();
    return false;
  }

  return r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED;
}

void
GlideConeGpuSession::Step(unsigned n) noexcept
{
  if (!active || remaining == 0 || n == 0 || fence != nullptr)
    return;

  glUseProgram(program);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, elev_buf);

  const unsigned batch = std::min(n, remaining);
  for (unsigned i = 0; i < batch; ++i) {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cell_cur);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, cell_next);
    glDispatchCompute(wg_x, wg_y, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    std::swap(cell_cur, cell_next);
  }
  remaining -= batch;
  fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

bool
GlideConeGpuSession::Finish(GlideConeResult &out) noexcept
{
  if (!active || remaining != 0 || !PollFence())
    return false;

  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

  const std::size_t count = std::size_t(width) * height;
  out.width = width;
  out.height = height;
  out.altitudes.resize(count);
  out.origin_x.resize(count);
  out.origin_y.resize(count);
  out.ground.resize(count);

  bool ok = true;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, cell_cur);
  const auto *mapped = static_cast<const GpuCell *>(
    glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, cell_bytes,
                     GL_MAP_READ_BIT));
  if (mapped != nullptr) {
    for (std::size_t i = 0; i < count; ++i) {
      out.altitudes[i] = mapped[i].alt;
      out.origin_x[i] = mapped[i].ox;
      out.origin_y[i] = mapped[i].oy;
      out.ground[i] = std::uint8_t(mapped[i].flags & 1u);
    }
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  } else {
    ok = false;
    out.Clear();
  }

  DeleteBuffers();
  active = false;
  remaining = 0;
  return ok;
}

#else // !HAVE_GLES_COMPUTE
#endif
