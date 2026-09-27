// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Runtime OpenGL ES 3.1 compute availability (glide cone GPU path).
 *
 * Set once from #EGL::Display::CreateContext when it successfully
 * creates (or fails to create) an ES 3.1 context.  Defaults to false
 * until that probe runs.
 */
#ifdef HAVE_GLES_COMPUTE

void
SetGLES31ComputeAvailable(bool available) noexcept;

[[gnu::pure]]
bool
HaveGLES31Compute() noexcept;

#else

constexpr bool
HaveGLES31Compute() noexcept
{
  return false;
}

#endif
