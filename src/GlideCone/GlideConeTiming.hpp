// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/*
 * Performance instrumentation for the GlideCone GPU paths (upward cone
 * and downward optional area).  Pure measurement: nothing here changes
 * what is computed.
 *
 * GLIDECONE_TIMING (default 1)
 *   Master switch.  Set to 0 to compile every timer, extra log line and
 *   GPU timer query out to nothing; the code is then identical in
 *   behaviour to the uninstrumented version.
 *
 * GLIDECONE_TIMING_GPU (default 1, only used when GLIDECONE_TIMING)
 *   Wrap the propagate dispatches and the change-sum dispatches in
 *   GL_EXT_disjoint_timer_query GL_TIME_ELAPSED_EXT queries when the
 *   extension is exposed.  Results are fetched only after the final
 *   readback fence has already completed, and only if
 *   GL_QUERY_RESULT_AVAILABLE says so, so this never adds a CPU-GPU
 *   sync of its own.
 *
 * GLIDECONE_TIMING_ISOLATE (default 0, only used when GLIDECONE_TIMING)
 *   Debug only: insert an extra GPU fence wait before the convergence
 *   checks' sum shader / upload so the CPU timers can attribute GPU
 *   time to the right phase.  This ADDS syncs and therefore changes the
 *   timings being measured (results stay identical).  Keep at 0 for
 *   representative numbers.
 *
 * Clock: std::chrono::steady_clock (same as the existing GlideCone
 * timers; CLOCK_MONOTONIC on Android/bionic), microsecond resolution.
 */

#ifndef GLIDECONE_TIMING
#define GLIDECONE_TIMING 1
#endif

#ifndef GLIDECONE_TIMING_GPU
#define GLIDECONE_TIMING_GPU 1
#endif

#ifndef GLIDECONE_TIMING_ISOLATE
#define GLIDECONE_TIMING_ISOLATE 0
#endif

#if GLIDECONE_TIMING

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifdef HAVE_GLES_COMPUTE
#include "ui/egl/System.hpp"
#include "ui/canvas/opengl/Extension.hpp"
#include <GLES3/gl31.h>
#endif

namespace GlideConeTiming {

using Clock = std::chrono::steady_clock;

/** Monotonic timestamp in microseconds. */
[[nodiscard]] inline std::uint64_t
NowUs() noexcept
{
  return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
    Clock::now().time_since_epoch()).count());
}

/** Microseconds since @p t0_us. */
[[nodiscard]] inline std::uint64_t
SinceUs(std::uint64_t t0_us) noexcept
{
  return NowUs() - t0_us;
}

[[nodiscard]] constexpr double
Ms(std::uint64_t us) noexcept
{
  return double(us) / 1000.;
}

/** Adds the lifetime of the object to an accumulator. */
class Scoped {
  std::uint64_t &acc;
  const std::uint64_t t0;

public:
  explicit Scoped(std::uint64_t &_acc) noexcept
    :acc(_acc), t0(NowUs()) {}

  ~Scoped() noexcept {
    acc += NowUs() - t0;
  }

  Scoped(const Scoped &) = delete;
  Scoped &operator=(const Scoped &) = delete;
};

/** min / avg / max / sum over a series of samples (microseconds). */
struct Stat {
  std::uint64_t sum = 0;
  std::uint64_t min = 0;
  std::uint64_t max = 0;
  unsigned n = 0;

  void Add(std::uint64_t us) noexcept {
    if (n == 0 || us < min)
      min = us;
    if (us > max)
      max = us;
    sum += us;
    ++n;
  }

  [[nodiscard]] double SumMs() const noexcept { return Ms(sum); }
  [[nodiscard]] double MinMs() const noexcept { return Ms(min); }
  [[nodiscard]] double MaxMs() const noexcept { return Ms(max); }
  [[nodiscard]] double AvgMs() const noexcept {
    return n > 0 ? Ms(sum) / n : 0.;
  }
};

/** Append "label:value" style samples to a compact log list. */
inline void
AppendSample(std::string &s, unsigned key, double ms,
             std::uint32_t extra) noexcept
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s%u:%.2f/%u",
                s.empty() ? "" : ",", key, ms, unsigned(extra));
  try {
    s += buf;
  } catch (...) {
  }
}

#if defined(HAVE_GLES_COMPUTE) && GLIDECONE_TIMING_GPU

/* Tokens from GL_EXT_disjoint_timer_query; defined locally so we do not
   have to mix GLES2/gl2ext.h into the ES 3.1 translation units. */
#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

using GetQueryObjectui64vFn = void (GL_APIENTRYP)(GLuint, GLenum, GLuint64 *);

struct GpuTimerSupport {
  bool available = false;
  /** Optional 64-bit result getter; nullptr falls back to the ES 3.0
      core 32-bit glGetQueryObjectuiv (saturates at ~4.29 s). */
  GetQueryObjectui64vFn get_ui64 = nullptr;
};

/**
 * Probe once (on the GLES compute thread, context current).  Uses the
 * repository's existing glGetString(GL_EXTENSIONS) parser and
 * eglGetProcAddress; glGenQueries/glBeginQuery/glEndQuery are ES 3.0
 * core and accept GL_TIME_ELAPSED_EXT when the extension is exposed.
 */
inline const GpuTimerSupport &
GetGpuTimerSupport() noexcept
{
  static const GpuTimerSupport support = []() noexcept {
    GpuTimerSupport s;
    s.available =
      OpenGL::IsExtensionSupported("GL_EXT_disjoint_timer_query");
    if (s.available)
      s.get_ui64 = reinterpret_cast<GetQueryObjectui64vFn>(
        eglGetProcAddress("glGetQueryObjectui64vEXT"));
    return s;
  }();
  return support;
}

/**
 * A set of GL_TIME_ELAPSED_EXT queries, each tagged with a category
 * (e.g. 0 = propagate, 1 = sum shader).  Only one may be open at a
 * time (extension rule).  The destructor closes and deletes everything,
 * so early returns (abort, map failure) are safe.
 */
class GpuTimerSet {
public:
  static constexpr unsigned KINDS = 2;

private:
  struct Query {
    GLuint id;
    unsigned kind;
  };

  std::vector<Query> queries;
  bool enabled;
  bool open = false;

public:
  GpuTimerSet() noexcept
    :enabled(GetGpuTimerSupport().available) {
    if (enabled) {
      /* reading the disjoint flag clears it */
      GLint disjoint = 0;
      glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    }
  }

  ~GpuTimerSet() noexcept {
    End();
    for (const auto &q : queries)
      glDeleteQueries(1, &q.id);
  }

  GpuTimerSet(const GpuTimerSet &) = delete;
  GpuTimerSet &operator=(const GpuTimerSet &) = delete;

  [[nodiscard]] bool IsEnabled() const noexcept {
    return enabled;
  }

  void Begin(unsigned kind) noexcept {
    if (!enabled || open || kind >= KINDS)
      return;
    GLuint id = 0;
    glGenQueries(1, &id);
    if (id == 0)
      return;
    try {
      queries.push_back({id, kind});
    } catch (...) {
      glDeleteQueries(1, &id);
      return;
    }
    glBeginQuery(GL_TIME_ELAPSED_EXT, id);
    open = true;
  }

  void End() noexcept {
    if (!open)
      return;
    glEndQuery(GL_TIME_ELAPSED_EXT);
    open = false;
  }

  struct Result {
    /** Summed GPU nanoseconds per kind (only ready queries). */
    std::uint64_t ns[KINDS] = {};
    unsigned ready = 0;
    unsigned pending = 0;
    bool disjoint = false;

    [[nodiscard]] double MsOf(unsigned kind) const noexcept {
      return kind < KINDS ? double(ns[kind]) / 1e6 : 0.;
    }
  };

  /**
   * Fetch results.  Call only after a fence covering every query has
   * already been waited for; queries not yet available are counted as
   * pending instead of blocking.
   */
  Result Collect() noexcept {
    Result r;
    if (!enabled)
      return r;
    End();
    const auto get_ui64 = GetGpuTimerSupport().get_ui64;
    for (const auto &q : queries) {
      GLuint available = 0;
      glGetQueryObjectuiv(q.id, GL_QUERY_RESULT_AVAILABLE, &available);
      if (!available) {
        ++r.pending;
        continue;
      }
      std::uint64_t ns = 0;
      if (get_ui64 != nullptr) {
        GLuint64 v = 0;
        get_ui64(q.id, GL_QUERY_RESULT, &v);
        ns = std::uint64_t(v);
      } else {
        GLuint v = 0;
        glGetQueryObjectuiv(q.id, GL_QUERY_RESULT, &v);
        ns = std::uint64_t(v);
      }
      r.ns[q.kind] += ns;
      ++r.ready;
    }
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    r.disjoint = disjoint != 0;
    return r;
  }
};

#endif // HAVE_GLES_COMPUTE && GLIDECONE_TIMING_GPU

} // namespace GlideConeTiming

#endif // GLIDECONE_TIMING

/*
 * Statement wrappers: GLIDECONE_TIMING_ONLY(stmt;) expands to stmt only
 * when timing is compiled in; GLIDECONE_GPU_TIMER(stmt;) only when the
 * GPU timer-query path is compiled in as well.
 */
#if GLIDECONE_TIMING
#define GLIDECONE_TIMING_ONLY(...) __VA_ARGS__
#else
#define GLIDECONE_TIMING_ONLY(...)
#endif

#if GLIDECONE_TIMING && GLIDECONE_TIMING_GPU && defined(HAVE_GLES_COMPUTE)
#define GLIDECONE_GPU_TIMER(...) __VA_ARGS__
#else
#define GLIDECONE_GPU_TIMER(...)
#endif
