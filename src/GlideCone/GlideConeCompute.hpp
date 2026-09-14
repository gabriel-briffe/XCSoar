// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeData.hpp"

/**
 * GPU (OpenGL ES 3.1 compute) implementation of the glide cone
 * propagation.  This is a GPU-only feature: on targets without a GLES 3.1
 * compute backend, Run() is a no-op returning false.
 */
namespace GlideConeCompute {

/** True if this build was compiled with a GLES 3.1 compute backend. */
[[gnu::const]]
constexpr bool Available() noexcept {
#ifdef HAVE_GLES_COMPUTE
  return true;
#else
  return false;
#endif
}

/**
 * Run the cone propagation on the GPU and read back the result.
 *
 * Must be called with the OpenGL context current (i.e. from the draw
 * thread during painting).  Returns false if the backend is unavailable
 * or on any GL error.
 */
bool Run(const GlideConeGrid &grid, GlideConeResult &out) noexcept;

} // namespace GlideConeCompute
