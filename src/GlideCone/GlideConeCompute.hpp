// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeData.hpp"

#include <functional>

#ifdef HAVE_GLES_COMPUTE
#include <GLES3/gl31.h>
#endif

/**
 * GPU (OpenGL ES 3.1 compute) glide cone propagation.
 *
 * All GL entry points must run with a GLES 3.1 context current (the
 * dedicated compute-thread shared context).  The destructor does not
 * touch GL — call DestroyGL() with the context current first.
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
  GLuint change_buf = 0;
  unsigned width = 0, height = 0;
  unsigned wg_x = 0, wg_y = 0;
  GLsizeiptr cell_bytes = 0;
  unsigned iterations_done = 0;
#endif

public:
  /** Iterations between abort checks on the compute thread. */
  static constexpr unsigned BATCH = 64;

  [[gnu::const]]
  static constexpr bool Available() noexcept {
#ifdef HAVE_GLES_COMPUTE
    return true;
#else
    return false;
#endif
  }

  [[gnu::pure]]
  bool IsActive() const noexcept {
    return active;
  }

  [[gnu::pure]]
  unsigned Remaining() const noexcept {
    return remaining;
  }

#ifdef HAVE_GLES_COMPUTE
  /**
   * Run the full iteration cap on the current context.  @p should_abort
   * is polled between batches; on abort the session is cancelled and
   * false is returned.  When @p hit_iteration_cap is non-null, it is set
   * true if Finish succeeded but the field never converged.
   */
  bool Run(const GlideConeGrid &grid,
           const std::function<bool()> &should_abort,
           GlideConeResult &out,
           bool *hit_iteration_cap = nullptr) noexcept;

  /** Drop buffers/program.  Context must be current. */
  void DestroyGL() noexcept;

  void Cancel() noexcept;

private:
  bool Begin(const GlideConeGrid &grid) noexcept;
  /**
   * Run up to @p n iterations.  Change-count is read only at batch end
   * once iterations_done exceeds max(width,height)/2+1, after a GPU
   * fence signals the batch finished.
   * @return true if the field converged (no cell changes in the batch).
   */
  bool Dispatch(unsigned n) noexcept;
  bool Finish(GlideConeResult &out) noexcept;
  void DeleteBuffers() noexcept;
  bool EnsureProgram() noexcept;
#else
  bool Run(const GlideConeGrid &,
           const std::function<bool()> &,
           GlideConeResult &,
           bool *hit_iteration_cap = nullptr) noexcept {
    if (hit_iteration_cap != nullptr)
      *hit_iteration_cap = false;
    return false;
  }

  void DestroyGL() noexcept {}

  void Cancel() noexcept {
    active = false;
    remaining = 0;
  }
#endif
};
