// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "util/AllocatedArray.hxx"
#include "Computer/TraceComputer.hpp"
#include "Engine/Trace/Point.hpp"
#include "Engine/Trace/Vector.hpp"
#include "Geo/GeoBounds.hpp"
#include "MapSettings.hpp"
#include "ui/dim/Point.hpp"
#include "ui/dim/BulkPoint.hpp"
#include "time/Stamp.hpp"
#include "util/Serial.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/Color.hpp"
#include "ui/canvas/opengl/Buffer.hpp"
#include "Math/Point2D.hpp"

#include <memory>
#endif

#include <vector>

class Canvas;
class TraceComputer;
class Projection;
class WindowProjection;
class ContestTraceVector;
struct ContestTracePoint;
struct TrailLook;
struct NMEAInfo;
struct DerivedInfo;
struct TrailSettings;

/**
 * Trail renderer
 * renders the trail of past position fixes on the map
 * includes filter for coarse-graining trail in LoadTrace
 */
class TrailRenderer {
  struct TrailPointData {
    PixelPoint point;
    double value{};
    TracePoint::Time time{};
  };

  /** Undrifted geo sample with metadata for per-frame drift projection. */
  struct CachedPathPoint {
    GeoPoint geo;
    TracePoint::Time time{};
    uint16_t drift_factor{};
  };

  /** Geo path with a single trail colour index (projected at draw time). */
  struct CachedColourRun {
    unsigned color_index{};
    std::vector<CachedPathPoint> points;
  };

  /** Tessellated, coloured geometry for one completed GPS leg. */
  struct CachedTrailSegment {
    std::vector<CachedColourRun> colour_runs;
  };

  /** Projection state that affects tessellation or visible trail query. */
  struct TrailDrawFingerprint {
    double scale_px_per_m{};
    GeoBounds query_bounds{};
    double color_min{};
    double color_max{};
    TrailSettings::Type settings_type{};

    [[gnu::pure]]
    bool operator==(const TrailDrawFingerprint &other) const noexcept;
  };

  /** Precomputed reciprocals for vario/altitude → colour index. */
  struct ColorScale {
    double neg_inv_min{};
    double inv_max{};
    double alt_inv_range{};
    double alt_min{};
    bool is_altitude{};

    [[gnu::pure]]
    unsigned Index(double value) const noexcept;

    static ColorScale FromMinMax(TrailSettings::Type type,
                                 double value_min,
                                 double value_max) noexcept;
  };

  const TrailLook &look;

  TracePointVector trace;
  std::vector<TrailVarioSample> merge_vario_samples;
  AllocatedArray<BulkPixelPoint> points;

  /**
   * Time-windowed store copy (no viewport filter).  Spatial filter runs
   * locally into #trace so pan/zoom need not re-walk the store under lock.
   */
  TracePointVector history;
  std::vector<TrailVarioSample> history_vario;
  bool history_valid = false;
  std::chrono::duration<unsigned> history_min_time{};
  Serial history_append_serial{};
  Serial history_modify_serial{};
  TrailQuery last_query{};

  /** Reused each Draw() to avoid per-frame heap allocations. */
  std::vector<TrailPointData> valid_points;
  std::vector<PixelPoint> interpolated;
  /** Segment parameter u in [0,1] → vario for merge-vario colouring. */
  std::vector<std::pair<double, double>> vario_breakpoints;

  std::vector<CachedTrailSegment> segment_cache;
  Serial synced_append_serial;
  Serial synced_modify_serial;
  Serial cache_append_serial;
  Serial cache_modify_serial;
  TrailDrawFingerprint fingerprint{};
  size_t merge_sample_search_index{};
  /** Keep completed-segment drift stable between GPS trace updates. */
  TimeStamp stable_drift_time{TimeStamp::Undefined()};

  /**
   * Colour scale frozen across pan/zoom.  Recomputed when the time-window
   * history or trail type changes, from the current draw #trace (local
   * contrast).  Using the full history extremes washed out Long trails.
   */
  bool color_scale_valid = false;
  ColorScale frozen_color_scale{};
  double frozen_color_value_min{};
  double frozen_color_value_max{};
  Serial color_scale_append_serial{};
  Serial color_scale_modify_serial{};
  TrailSettings::Type color_scale_type{};
  std::chrono::duration<unsigned> color_scale_min_time{};

public:
  TrailRenderer(const TrailLook &_look) noexcept:look(_look) {}

  /**
   * Load the full trace into this object.
   */
  bool LoadTrace(const TraceComputer &trace_computer) noexcept;

  /**
   * Load a filtered trace into this object.
   */
  bool LoadTrace(const TraceComputer &trace_computer,
                 TimeStamp min_time,
                 const WindowProjection &projection) noexcept;

  [[gnu::pure]]
  static TrailQuery MakeTrailQuery(TimeStamp min_time,
                                   const WindowProjection &projection,
                                   bool keep_all=false) noexcept;

  void ScanBounds(GeoBounds &bounds) const noexcept {
    trace.ScanBounds(bounds);
  }

  void Draw(Canvas &canvas, const TraceComputer &trace_computer,
            const WindowProjection &projection,
            TimeStamp min_time,
            bool enable_traildrift, PixelPoint pos, const NMEAInfo &basic,
            const DerivedInfo &calculated,
            const TrailSettings &settings) noexcept;

  /**
   * Draw the trace that was obtained by LoadTrace() with the trace pen.
   */
  void Draw(Canvas &canvas, const WindowProjection &projection) noexcept;

  void Draw(Canvas &canvas, const TraceComputer &trace_computer,
            const WindowProjection &projection,
            TimeStamp min_time={}) noexcept;

  [[gnu::malloc]]
  BulkPixelPoint *Prepare(unsigned n) noexcept;

  void DrawPreparedPolyline(Canvas &canvas, unsigned n) noexcept;
  void DrawPreparedPolygon(Canvas &canvas, unsigned n) noexcept;

  /**
   * Draw a ContestTraceVector.  The caller must select a Pen.
   */
  void DrawTraceVector(Canvas &canvas, const Projection &projection,
                       const ContestTraceVector &trace) noexcept;

  /**
   * Draw a triangle described by trace index 1..3; expected trace
   * size is 5 (0=start, 4=finish).
   */
  void DrawTriangle(Canvas &canvas, const Projection &projection,
                    const ContestTraceVector &trace) noexcept;

private:
  void SelectTrailPen(Canvas &canvas, unsigned color_index,
                      bool scaled_trail) const noexcept;

  void DrawColourPolyline(Canvas &canvas, unsigned color_index,
                          TrailSettings::Type type,
                          bool scaled_trail,
                          const std::vector<PixelPoint> &pts,
                          size_t first, size_t last) noexcept;

  void DrawRibbonPolyline(Canvas &canvas, unsigned color_index,
                          const BulkPixelPoint *pts, unsigned n) noexcept;

#ifdef ENABLE_OPENGL
  /** Expand ribbon segments into #ribbon_vertices / #ribbon_colors. */
  void AppendRibbonGeometry(unsigned color_index,
                            const BulkPixelPoint *pts,
                            unsigned n) noexcept;

  /** Draw and clear the batched ribbon triangle list. */
  void FlushRibbonBatch() noexcept;

  std::vector<BulkPixelPoint> ribbon_vertices;
  std::vector<Color> ribbon_colors;
#endif

  void DrawCachedSegments(Canvas &canvas,
                          const WindowProjection &projection,
                          TrailSettings::Type type,
                          bool scaled_trail,
                          bool enable_traildrift,
                          const GeoPoint &traildrift,
                          TimeStamp drift_now,
                          const std::vector<CachedTrailSegment> &segments) noexcept;

#ifdef ENABLE_OPENGL
  /**
   * Draw unthinned Full trail with geo vertices and GPU projection
   * (#ToGLM).  Pan/zoom only updates the modelview matrix.
   */
  void DrawFullTrailGPU(Canvas &canvas,
                        const WindowProjection &projection,
                        TrailSettings::Type type,
                        bool scaled_trail,
                        const ColorScale &color_scale,
                        bool use_smoothing,
                        unsigned num_segments,
                        const NMEAInfo &basic,
                        PixelPoint aircraft_pos,
                        const TrailSettings &settings) noexcept;

  void RebuildFullTrailGeoRuns(const ColorScale &color_scale,
                               TrailSettings::Type type,
                               bool use_smoothing,
                               unsigned num_segments) noexcept;

  /**
   * Append geometry for new store points.  With smoothing: finalize the
   * previous provisional tip with Catmull-Rom, then append a new straight
   * tip leg (needs the next fix as Catmull g3).  Old VBO content stays.
   */
  void AppendFullTrailGeoLegs(const ColorScale &color_scale,
                              TrailSettings::Type type,
                              bool use_smoothing,
                              unsigned num_segments,
                              size_t from_point) noexcept;

  void UploadFullTrailVBO(size_t from_vertex) noexcept;

  void TruncateFullTrailVertices(size_t new_size) noexcept;

  void EmitFullTrailLeg(const ColorScale &color_scale,
                        TrailSettings::Type type,
                        bool use_smoothing,
                        unsigned num_segments,
                        size_t leg_index) noexcept;

  /** One GL_LINE_STRIP range inside #full_trail_vertices / VBO. */
  struct FullTrailDrawRange {
    unsigned color_index{};
    unsigned first{};
    unsigned count{};
  };

  void AppendFullTrailVertex(unsigned color_index,
                             FloatPoint2D p) noexcept;

  std::vector<FloatPoint2D> full_trail_vertices;
  std::vector<FullTrailDrawRange> full_trail_ranges;
  std::unique_ptr<GLDynamicArrayBuffer> full_trail_vbo;
  size_t full_trail_vbo_capacity = 0;
  size_t full_trail_point_count = 0;
  /** Start of provisional (non-Catmull) tip verts; truncated on next append. */
  size_t full_trail_pending_vertex = 0;
  bool full_trail_smoothing_enabled = false;
  GeoPoint full_trail_reference = GeoPoint::Invalid();
  Serial full_trail_append_serial{};
  Serial full_trail_modify_serial{};
  TrailSettings::Type full_trail_type{};
  double full_trail_color_min{};
  double full_trail_color_max{};
#endif

  [[gnu::pure]]
  static PixelPoint ProjectCachedPathPoint(const CachedPathPoint &p,
                                           const WindowProjection &projection,
                                           bool enable_traildrift,
                                           const GeoPoint &traildrift,
                                           TimeStamp drift_now) noexcept;

  static void ProjectCachedColourRun(const CachedColourRun &run,
                                     size_t start_index,
                                     const WindowProjection &projection,
                                     bool enable_traildrift,
                                     const GeoPoint &traildrift,
                                     TimeStamp drift_now,
                                     BulkPixelPoint *buffer,
                                     unsigned &n,
                                     bool simplify_projected) noexcept;

  void DrawVarioColouredPolyline(Canvas &canvas,
                                 const std::vector<PixelPoint> &pts,
                                 TrailSettings::Type type,
                                 const TrailPointData &prev_data,
                                 const TrailPointData &curr_data,
                                 const ColorScale &color_scale,
                                 bool scaled_trail,
                                 bool use_merge_vario,
                                 unsigned num_pieces) noexcept;

  static void AppendColourRun(std::vector<CachedColourRun> &runs,
                              unsigned color_index,
                              const CachedPathPoint &pt) noexcept;

  [[gnu::pure]]
  static CachedPathPoint LerpCachedPathPoint(const GeoPoint &g0,
                                             const GeoPoint &g1,
                                             TracePoint::Time t0,
                                             TracePoint::Time t1,
                                             unsigned df0, unsigned df1,
                                             double u) noexcept;

  void BuildDirectColourRuns(const TrailPointData &prev_data,
                             const TrailPointData &curr_data,
                             const GeoPoint &prev_geo,
                             const GeoPoint &curr_geo,
                             unsigned prev_drift_factor,
                             unsigned curr_drift_factor,
                             const std::vector<PixelPoint> &segment_pts,
                             TrailSettings::Type type,
                             const ColorScale &color_scale,
                             bool use_merge_vario,
                             std::vector<CachedColourRun> &runs) noexcept;

  void BuildSmoothColourRuns(const GeoPoint &g0, const GeoPoint &g1,
                             const GeoPoint &g2, const GeoPoint &g3,
                             TracePoint::Time time1,
                             TracePoint::Time time2,
                             unsigned drift_factor1,
                             unsigned drift_factor2,
                             unsigned num_segments,
                             const TrailPointData &prev_data,
                             const TrailPointData &curr_data,
                             TrailSettings::Type type,
                             const ColorScale &color_scale,
                             bool use_merge_vario,
                             std::vector<CachedColourRun> &runs) noexcept;

  /** Inline Catmull-Rom pieces for the bounded smoothed tail (#2664). */
  void DrawSmoothTailSegmentInline(Canvas &canvas,
                                   const PixelPoint &p0, const PixelPoint &p1,
                                   const PixelPoint &p2, const PixelPoint &p3,
                                   unsigned num_segments,
                                   const TrailPointData &prev_data,
                                   const TrailPointData &curr_data,
                                   TrailSettings::Type type,
                                   const ColorScale &color_scale,
                                   bool scaled_trail,
                                   bool use_merge_vario) noexcept;

  bool SyncTrace(const TraceComputer &trace_computer,
                 const TrailQuery &query) noexcept;

  void InvalidateHistory() noexcept;
  void InvalidateSegmentCache() noexcept;

  void RefilterTraceFromHistory(const TrailSpatialFilter &filter) noexcept;

  [[gnu::pure]]
  static bool TrailQueryViewEqual(const TrailQuery &a,
                                  const TrailQuery &b) noexcept;

  void UpdateSegmentCache(const WindowProjection &projection,
                          TrailSettings::Type type,
                          const ColorScale &color_scale,
                          bool use_smoothing,
                          unsigned num_segments,
                          size_t first_smoothed_point,
                          size_t from_leg,
                          bool rebuild) noexcept;

  void MergeAdjacentColourRuns(std::vector<CachedColourRun> &runs) noexcept;

  void BuildCachedSegment(const WindowProjection &projection,
                          size_t leg_index,
                          TrailSettings::Type type,
                          const ColorScale &color_scale,
                          bool use_smoothing,
                          unsigned num_segments,
                          size_t first_smoothed_point,
                          size_t &merge_sample_index,
                          CachedTrailSegment &dest) noexcept;

  void DrawOpenLeg(Canvas &canvas,
                   const TrailSettings &settings,
                   const ColorScale &color_scale,
                   bool scaled_trail,
                   bool use_smoothing,
                   unsigned num_segments,
                   size_t first_smoothed_point,
                   const NMEAInfo &basic,
                   PixelPoint aircraft_pos) noexcept;

  void DrawTraceVector(Canvas &canvas, const Projection &projection,
                       const TracePointVector &trace) noexcept;
};
