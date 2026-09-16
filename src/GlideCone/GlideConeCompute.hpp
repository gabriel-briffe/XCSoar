// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeData.hpp"

#ifdef HAVE_GLES_COMPUTE
#include <GLES3/gl31.h>
#endif

/**
 * GPU (OpenGL ES 3.1 compute) glide cone propagation.
 *
 * On targets without a GLES 3.1 compute backend every method is a
 * no-op.  With compute, Begin/Step/Finish must run on the draw thread
 * with the GL context current.  Cancel() must also be called with the
 * context current; the destructor does not touch GL.
 */
class GlideConeGpuSession {
  bool active = false;
  unsigned remaining = 0;

#ifdef HAVE_GLES_COMPUTE
  GLuint program = 0;
  GLuint elev_buf = 0;
  GLuint cell_a = 0;
  GLuint cell_b = 0;
  GLuint cell_cur = 0;
  GLuint cell_next = 0;
  GLsync fence = nullptr;
  unsigned width = 0, height = 0;
  unsigned wg_x = 0, wg_y = 0;
  GLsizeiptr cell_bytes = 0;
#endif

public:
  static constexpr unsigned BATCH = 12;

  [[gnu::const]]
  static constexpr bool Available() noexcept {
#ifdef HAVE_GLES_COMPUTE
    return true;
#else
    return false;
#endif
  }

  /**
   * True after a successful Begin() until Finish() or Cancel().
   */
  [[gnu::pure]]
  bool IsActive() const noexcept {
    return active;
  }

  [[gnu::pure]]
  unsigned Remaining() const noexcept {
    return remaining;
  }

#ifdef HAVE_GLES_COMPUTE
  ~GlideConeGpuSession() noexcept = default;

  bool Begin(const GlideConeGrid &grid) noexcept;

  /**
   * If a batch fence is outstanding, poll it with timeout 0 (no
   * flush).  Returns true when there is no pending fence (ready to
   * Step or Finish).
   */
  bool PollFence() noexcept;

  /** Dispatch up to @p n iterations and insert a fence. */
  void Step(unsigned n) noexcept;

  bool Finish(GlideConeResult &out) noexcept;

  /** Drop GPU objects for the current job.  Context must be current. */
  void Cancel() noexcept;

private:
  void DeleteFence() noexcept;
  void DeleteBuffers() noexcept;
  bool EnsureProgram() noexcept;
#else
  bool Begin(const GlideConeGrid &) noexcept {
    return false;
  }

  bool PollFence() noexcept {
    return true;
  }

  void Step(unsigned) noexcept {}

  bool Finish(GlideConeResult &) noexcept {
    return false;
  }

  void Cancel() noexcept {
    active = false;
    remaining = 0;
  }
#endif
};
