// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "GlideConeField.hpp"
#include "Geo/GeoPoint.hpp"
#include "thread/Mutex.hxx"

#include <cstddef>
#include <cstdint>

class Canvas;
class WindowProjection;
class RasterTerrain;
struct MapLook;
struct ComputerSettings;

/**
 * Owns the glide cone GPU computation and draws the resulting relay path
 * on the moving map.
 *
 * The "Goto" target is submitted from the UI thread via SetTarget(); the
 * expensive GPU compute and the path drawing happen on the draw thread
 * (where the OpenGL context is current) inside Draw().  Only the pending
 * target is shared across threads and is protected by a mutex; the
 * computed field is owned by the draw thread.
 */
class GlideConeRenderer {
  Mutex mutex;

  /* shared pending target (guarded by #mutex) */
  GeoPoint pending_seed = GeoPoint::Invalid();
  double pending_seed_alt = 0;
  bool pending_valid = false;
  std::uint64_t pending_generation = 0;

  /* draw-thread-owned computed state */
  GlideConeField field;
  GeoPoint computed_seed = GeoPoint::Invalid();
  std::size_t computed_signature = 0;
  bool have_field = false;

  /* used to emit one diagnostic log line per new request */
  GeoPoint last_diag_seed = GeoPoint::Invalid();
  std::size_t last_diag_signature = ~std::size_t{0};
  bool last_diag_valid = false;

public:
  /**
   * Set the airport for which the glide cone should be computed.  May be
   * called from any thread.
   */
  void SetTarget(GeoPoint seed, double elevation) noexcept;

  /** Forget the current target.  May be called from any thread. */
  void ClearTarget() noexcept;

  /**
   * Draw the glide cone path.  Must be called on the draw thread with the
   * OpenGL context current.
   *
   * @param target the active navigation target (Goto/task destination)
   * used as the cone seed; falls back to the last explicit Goto target
   * when invalid
   */
  void Draw(Canvas &canvas, const WindowProjection &projection,
            GeoPoint aircraft, bool aircraft_valid,
            GeoPoint target, bool target_valid,
            const ComputerSettings &settings,
            const RasterTerrain *terrain, const MapLook &look) noexcept;

private:
  bool BuildField(GeoPoint seed, double elevation,
                  const ComputerSettings &settings,
                  const RasterTerrain &terrain) noexcept;
};
