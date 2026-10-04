// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Geo/GeoPoint.hpp"
#include "Math/Angle.hpp"
#include "ui/dim/Point.hpp"
#include "ui/dim/Size.hpp"

#include <chrono>
#include <optional>
#include <span>
#include <vector>

class Canvas;
class WindowProjection;
struct GlideConeField;
struct MapLook;
struct GlideConeSettings;

/**
 * Contour strokes, altitude labels, and relay-path traces for an
 * installed #GlideConeField.  Owns the geo label cache and settle
 * debounce; does not own workers or the field itself.
 */
class GlideConeOverlay {
  struct ContourLabel {
    GeoPoint location;
    /** Point ahead along the contour for screen-tangent orientation. */
    GeoPoint along;
    int level;
    PixelSize text_size;
    char text[32];
  };

  std::vector<ContourLabel> contour_labels;
  double label_cache_map_scale = -1;
  unsigned label_cache_spacing = 0;
  unsigned label_cache_font_h = 0;
  PixelSize label_cache_screen_size{};
  GeoPoint label_cache_center = GeoPoint::Invalid();
  Angle label_cache_angle = Angle::Zero();

  /**
   * Hold label rebuilds from the moment the compute center moves until
   * new contours are placed — covers the cooldown gap before awaiting_*.
   */
  bool labels_hold_rebase = false;

  /** Settle-debounce for label rebuilds (timer restarts while view moves). */
  bool label_rebuild_pending = false;
  std::chrono::steady_clock::time_point label_rebuild_since{};
  double label_rebuild_watch_scale = -1;
  GeoPoint label_rebuild_watch_center = GeoPoint::Invalid();
  Angle label_rebuild_watch_angle = Angle::Zero();
  /** Last logged debounce reason (avoid spamming identical lines). */
  const char *label_debounce_reason = nullptr;

public:
  void InvalidateLabels() noexcept;

  void HoldRebase() noexcept;

  void ClearHoldRebase() noexcept;

  [[gnu::pure]]
  bool IsHoldRebase() const noexcept {
    return labels_hold_rebase;
  }

  /**
   * Drop or keep the geo label cache after InstallField.  Terrain /
   * settings refreshes keep the same origin; only a center rebase
   * should invalidate.
   */
  void OnFieldInstalled(bool drop_labels) noexcept;

  /** Called when fresh contour polylines arrive on the field. */
  void OnContoursReady() noexcept;

  /**
   * Stroke worst-case relay path(s).  Returns red-disc centres to draw
   * later (above the green highest-arrival path).
   */
  std::vector<PixelPoint>
  DrawTraces(Canvas &canvas, const WindowProjection &projection,
             const GlideConeField &field,
             GeoPoint aircraft, bool aircraft_valid,
             std::optional<double> aircraft_altitude,
             GeoPoint pan_probe,
             std::optional<double> pan_altitude,
             const MapLook &look) const noexcept;

  /** Draw critical discs collected from #DrawTraces (top-most). */
  void DrawCriticalDiscs(Canvas &canvas,
                         std::span<const PixelPoint> discs) const noexcept;

  /**
   * Stroke contour lines and manage / draw altitude labels when
   * contours are enabled and visible.
   *
   * @param hold_labels  True while grid/GPU/contours are in flight or
   *                     a center rebase is pending (skips rebuild).
   * @param awaiting_grid / @p awaiting_gpu / @p awaiting_contours
   *                     Only used for hold-reason logging.
   */
  void DrawContours(Canvas &canvas, const WindowProjection &projection,
                    const GlideConeField &field,
                    const GlideConeSettings &gc, const MapLook &look,
                    bool hold_labels,
                    bool awaiting_grid, bool awaiting_gpu,
                    bool awaiting_contours) noexcept;

private:
  void RebuildLabels(Canvas &canvas, const WindowProjection &projection,
                     const GlideConeField &field,
                     const GlideConeSettings &gc,
                     const MapLook &look) noexcept;

  void DrawLabels(Canvas &canvas, const WindowProjection &projection,
                  const MapLook &look) const noexcept;

  /**
   * @param pan_path  Pan-crosshair worst-case path: always mark the
   *                  first air→ground contact (or start from a ground
   *                  cell with no disc), then use the 80% L/D budget.
   */
  void DrawTraceFrom(Canvas &canvas, const WindowProjection &projection,
                     const GlideConeField &field, GeoPoint from,
                     std::optional<double> start_altitude, bool pan_path,
                     const MapLook &look,
                     std::vector<PixelPoint> &critical) const noexcept;
};
