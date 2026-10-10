// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeData.hpp"

#include <functional>

/**
 * CPU upward glide-cone (Dijkstra).  Same rules as the GLES
 * PROPAGATE_SHADER: extended Bresenham LOS, ground freeze at elevation,
 * origin election with neighbour fallback.
 *
 * @return false if aborted via @p should_abort.
 */
bool
PropagateUpwardCpu(const GlideConeGrid &grid,
                   const std::function<bool()> &should_abort,
                   GlideConeResult &out,
                   bool *hit_iteration_cap = nullptr) noexcept;
