// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TrailRenderer.hpp"
#include "Look/TrailLook.hpp"
#include "ui/canvas/Canvas.hpp"
#include "NMEA/Info.hpp"
#include "NMEA/Derived.hpp"
#include "MapSettings.hpp"
#include "Computer/TraceComputer.hpp"
#include "Projection/WindowProjection.hpp"
#include "Geo/Math.hpp"
#include "Geo/GeoBounds.hpp"
#include "Engine/Contest/ContestTrace.hpp"
#include "Screen/Layout.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Shaders.hpp"
#include "ui/canvas/opengl/Program.hpp"
#include "ui/canvas/opengl/VertexPointer.hpp"
#include "ui/canvas/opengl/Buffer.hpp"
#include "ui/canvas/opengl/Color.hpp"
#include "ui/canvas/opengl/Geo.hpp"
#include "Math/Angle.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#endif

#include <algorithm>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include <cmath>

static constexpr double TRAIL_ZOOMED_OUT_MAP_SCALE = 6000;
static constexpr double TRAIL_BOUNDS_SCALE = 4.;

/**
 * On-screen trail sample spacing in points (1/72").  Converted to pixels
 * via Layout::PtScale (DPI / UI scale) then to metres via the projection,
 * so thinning tracks physical size across phones, OV boxes, and Kobo.
 */
static constexpr unsigned TRAIL_SPACING_PT_MIN = 2;
static constexpr unsigned TRAIL_SPACING_PT_MAX = 18;

/**
 * Grow spacing with zoom-out: GetMapScale / this → pixel target before
 * PtScale clamps.  Smaller → more thinning when zoomed out.  Still only
 * feeds the px→m pipeline (not a device-specific stride ladder).
 */
static constexpr double TRAIL_SPACING_MAP_SCALE_DIV = 250.;

namespace {

/**
 * Merge-vario samples in the half-open GPS interval [t0, t1).
 * \a search_from advances monotonically across successive segments.
 */
struct MergeSampleRange {
  size_t begin{};
  size_t end{}; /* one past the last sample in range */
};

static MergeSampleRange
FindMergeSamplesBetween(TracePoint::Time t0, TracePoint::Time t1,
                        const std::vector<TrailVarioSample> &samples,
                        size_t &search_from) noexcept
{
  while (search_from < samples.size() && samples[search_from].time < t0)
    ++search_from;

  const size_t begin = search_from;
  while (search_from < samples.size() && samples[search_from].time < t1)
    ++search_from;

  return {begin, search_from};
}

[[gnu::pure]]
static size_t
FindMergeSampleIndexAtOrAfter(
    TracePoint::Time time,
    const std::vector<TrailVarioSample> &samples) noexcept
{
  return std::lower_bound(samples.begin(), samples.end(), time,
                          [](const TrailVarioSample &sample,
                             TracePoint::Time t) noexcept {
                            return sample.time < t;
                          }) - samples.begin();
}

void
BuildVarioBreakpoints(TracePoint::Time t0, double v0,
                      TracePoint::Time t1, double v1,
                      const std::vector<TrailVarioSample> &samples,
                      size_t &merge_sample_index,
                      std::vector<std::pair<double, double>> &bps) noexcept
{
  bps.clear();
  if (!(t1 > t0))
    return;

  const MergeSampleRange range =
    FindMergeSamplesBetween(t0, t1, samples, merge_sample_index);

  const size_t n = range.end - range.begin;
  bps.reserve(n + 2);
  bps.emplace_back(0., v0);
  if (n == 0) {
    bps.emplace_back(1., v1);
    return;
  }

  const double dt = (double)(t1 - t0).count();

  auto sample_u = [&](size_t i) noexcept -> double {
    if (dt > 0.)
      return (double)(samples[range.begin + i].time - t0).count() / dt;
    return (double)(i + 1) / (double)(n + 1);
  };

  double u_prev = 0.;
  for (size_t i = 0; i < n; ) {
    const double u_i = sample_u(i);
    size_t j = i + 1;
    while (j < n && sample_u(j) == u_i)
      ++j;

    const double u_right = (j < n) ? sample_u(j) : 1.;
    const size_t m = j - i;
    const double u_left = u_prev;
    for (size_t k = i; k < j; ++k) {
      const double u = (m > 1)
        ? u_left + (u_right - u_left) * (double)(k - i + 1) / (double)(m + 1)
        : u_i;
      bps.emplace_back(u, (double)samples[range.begin + k].vario);
      u_prev = u;
    }
    i = j;
  }

  bps.emplace_back(1., v1);
}

[[gnu::pure]]
double
LookupVarioAtU(double u,
               const std::vector<std::pair<double, double>> &bps) noexcept
{
  if (bps.empty())
    return 0;
  if (u <= bps.front().first)
    return bps.front().second;
  if (u >= bps.back().first)
    return bps.back().second;

  const auto it = std::upper_bound(bps.begin(), bps.end(), u,
                                   [](double u, const auto &bp) {
                                     return u < bp.first;
                                   });
  const auto &b = *it;
  const auto &a = *std::prev(it);
  const double du = b.first - a.first;
  if (du <= 0.)
    return b.second;
  const double t = (u - a.first) / du;
  return a.second * (1. - t) + b.second * t;
}

[[gnu::const]]
static bool
IsVarioDotsOnlyMode(TrailSettings::Type type) noexcept
{
  return type == TrailSettings::Type::VARIO_1_DOTS ||
    type == TrailSettings::Type::VARIO_2_DOTS;
}

[[gnu::const]]
static bool
IsVarioLineMode(TrailSettings::Type type) noexcept
{
  return type == TrailSettings::Type::VARIO_1 ||
    type == TrailSettings::Type::VARIO_2;
}

[[gnu::pure]]
static double
InterpolatePieceValue(TrailSettings::Type type,
                      double prev_value,
                      double curr_value,
                      unsigned piece_index,
                      double piece_count,
                      bool use_merge_vario,
                      const std::vector<std::pair<double, double>> &bps) noexcept
{
  if (type == TrailSettings::Type::ALTITUDE) {
    const double t = (piece_index + 1) / piece_count;
    return prev_value * (1.0 - t) + curr_value * t;
  }

  if (use_merge_vario && !bps.empty()) {
    const double u = (piece_index + 0.5) / piece_count;
    return LookupVarioAtU(u, bps);
  }

  const double t = (piece_index + 1) / piece_count;
  return prev_value * (1.0 - t) + curr_value * t;
}

struct CatmullRomWeights {
  double c0, c1, c2, c3;
};

[[gnu::const]]
static CatmullRomWeights
ComputeCatmullRomWeights(double t) noexcept
{
  const double t2 = t * t;
  const double t3 = t2 * t;
  return {
    -0.5 * t3 + t2 - 0.5 * t,
    1.5 * t3 - 2.5 * t2 + 1.0,
    -1.5 * t3 + 2.0 * t2 + 0.5 * t,
    0.5 * t3 - 0.5 * t2,
  };
}

/**
 * Target on-screen spacing between kept fixes, in pixels.
 * Floor/ceiling use Layout::PtScale so the same physical size on glass
 * applies across DPI/resolution; zoom only moves within that range.
 * Metres come from WindowProjection::DistancePixelsToMeters in
 * MakeTrailQuery — not from magic GetMapScale stride bands.
 */
[[gnu::pure]]
static int
GetTrailSpacingPixels(double map_scale) noexcept
{
  const int min_px =
    std::max(1, int(Layout::PtScale(TRAIL_SPACING_PT_MIN)));
  const int max_px =
    std::max(min_px, int(Layout::PtScale(TRAIL_SPACING_PT_MAX)));
  const int from_zoom =
    int(map_scale / TRAIL_SPACING_MAP_SCALE_DIV);
  return std::clamp(from_zoom, min_px, max_px);
}

/** Max number of recent trace points that receive Catmull-Rom smoothing. */
static constexpr size_t MAX_SMOOTHED_TRAIL_POINTS = 180;

/**
 * Fixed Catmull-Rom sub-divisions per GPS leg in the smoothed tail.
 * Constant across zoom levels for visual consistency and bounded cost.
 */
static constexpr unsigned TRAIL_SMOOTH_SEGMENTS = 4;

[[gnu::const]]
static size_t
GetFirstSmoothedPointIndex(size_t point_count) noexcept
{
  return point_count > MAX_SMOOTHED_TRAIL_POINTS
    ? point_count - MAX_SMOOTHED_TRAIL_POINTS
    : 0;
}

[[gnu::pure]]
static bool
UseTrailSmoothing(TrailSettings::Type type, double map_scale) noexcept
{
  if (IsVarioDotsOnlyMode(type) || map_scale > TRAIL_ZOOMED_OUT_MAP_SCALE)
    return false;

  return true;
}

[[gnu::const]]
static bool
UseRibbonTrail(TrailSettings::Type type, bool scaled_trail) noexcept
{
  return scaled_trail && IsVarioLineMode(type);
}

[[gnu::const]]
static PixelPoint
RoundRibbonPoint(double x, double y) noexcept
{
  return {int(std::lround(x)), int(std::lround(y))};
}

[[gnu::pure]]
static bool
IsLowDpiTrail() noexcept
{
  return Layout::min_screen_pixels <= TrailLook::LOW_DPI_TRAIL_SCREEN_PX;
}

static void
AppendFilteredTrailPoint(BulkPixelPoint *buffer, unsigned &n,
                         PixelPoint pt, bool simplify) noexcept
{
  if (n > 0 && pt.x == buffer[n - 1].x && pt.y == buffer[n - 1].y)
    return;

  if (simplify && n >= 2) {
    const auto &a = buffer[n - 2];
    const auto &b = buffer[n - 1];

    if (std::abs(pt.x - a.x) <= 1 && std::abs(pt.y - a.y) <= 1) {
      buffer[n - 1] = pt;
      return;
    }

    if ((a.x == b.x && b.x == pt.x) || (a.y == b.y && b.y == pt.y)) {
      buffer[n - 1] = pt;
      return;
    }
  }

  buffer[n++] = pt;
}

[[gnu::pure]]
static double
GetRibbonWidth(const TrailLook &look, unsigned color_index) noexcept
{
  const unsigned min_width = look.trail_widths[0];
  const unsigned width = look.trail_widths[color_index];

  if (width <= min_width)
    return min_width;

  const double extra_factor = IsLowDpiTrail() ? 0.5 : 1.0;
  return min_width + (width - min_width) * extra_factor;
}

[[gnu::pure]]
static PixelPoint
LerpPixelPoint(const PixelPoint &a, const PixelPoint &b,
               double u) noexcept
{
  return PixelPoint(
    int(std::lround(a.x + (b.x - a.x) * u)),
    int(std::lround(a.y + (b.y - a.y) * u)));
}

static void
BuildDirectSegmentPoints(const PixelPoint &p1, const PixelPoint &p2,
                         const std::vector<std::pair<double, double>> &bps,
                         bool use_merge_vario,
                         std::vector<PixelPoint> &result) noexcept
{
  result.clear();
  result.push_back(p1);

  if (use_merge_vario && bps.size() > 2) {
    const unsigned pieces = unsigned(bps.size() - 1);
    for (unsigned j = 1; j < pieces; ++j)
      result.push_back(LerpPixelPoint(p1, p2, double(j) / double(pieces)));
  }

  result.push_back(p2);
}

/**
 * Catmull-Rom spline interpolation between two points.
 */
[[gnu::pure]]
static PixelPoint
CatmullRomInterpolate(const PixelPoint &p0, const PixelPoint &p1,
                      const PixelPoint &p2, const PixelPoint &p3,
                      double t) noexcept
{
  const CatmullRomWeights c = ComputeCatmullRomWeights(t);

  return PixelPoint(
    static_cast<int>(c.c0 * p0.x + c.c1 * p1.x + c.c2 * p2.x + c.c3 * p3.x),
    static_cast<int>(c.c0 * p0.y + c.c1 * p1.y + c.c2 * p2.y + c.c3 * p3.y)
  );
}

[[gnu::pure]]
static GeoPoint
TrailGeoPoint(const TracePoint &tp, bool enable_traildrift,
              const GeoPoint &traildrift,
              TimeStamp now) noexcept
{
  if (!enable_traildrift)
    return tp.GetLocation();

  return tp.GetLocation().Parametric(traildrift,
                                     tp.CalculateDrift(now));
}

[[gnu::pure]]
static GeoPoint
CatmullRomGeo(const GeoPoint &p0, const GeoPoint &p1,
              const GeoPoint &p2, const GeoPoint &p3,
              double t) noexcept
{
  const CatmullRomWeights c = ComputeCatmullRomWeights(t);

  return GeoPoint(
    Angle::Native(c.c0 * p0.longitude.Native() + c.c1 * p1.longitude.Native() +
                  c.c2 * p2.longitude.Native() + c.c3 * p3.longitude.Native()),
    Angle::Native(c.c0 * p0.latitude.Native() + c.c1 * p1.latitude.Native() +
                  c.c2 * p2.latitude.Native() + c.c3 * p3.latitude.Native()));
}

[[gnu::pure]]
static GeoPoint
DriftGeoPoint(const GeoPoint &geo, TracePoint::Time time,
              uint16_t drift_factor,
              bool enable_traildrift,
              const GeoPoint &traildrift,
              TimeStamp now) noexcept
{
  if (!enable_traildrift)
    return geo;

  const double dt =
    (now.ToDuration() - std::chrono::duration_cast<FloatDuration>(time)).count();
  return geo.Parametric(traildrift, dt * drift_factor / 256);
}

} // namespace

void
TrailRenderer::MergeAdjacentColourRuns(
    std::vector<CachedColourRun> &runs) noexcept
{
  if (runs.size() < 2)
    return;

  size_t write = 0;
  for (size_t read = 1; read < runs.size(); ++read) {
    if (runs[read].color_index == runs[write].color_index) {
      const auto &src = runs[read].points;
      if (src.empty())
        continue;

      size_t start = 0;
      if (!runs[write].points.empty() &&
          runs[write].points.back().geo == src.front().geo)
        start = 1;

      runs[write].points.insert(runs[write].points.end(),
                                src.begin() + start, src.end());
    } else {
      ++write;
      if (write != read)
        runs[write] = std::move(runs[read]);
    }
  }
  runs.resize(write + 1);
}

bool
TrailRenderer::LoadTrace(const TraceComputer &trace_computer) noexcept
{
  InvalidateHistory();
  InvalidateSegmentCache();
  trace.clear();
  merge_vario_samples.clear();
  try {
    trace_computer.LockedCopySnapshot(trace, merge_vario_samples);
  } catch (const std::bad_alloc &) {
    trace.clear();
    merge_vario_samples.clear();
    return false;
  }
  return !trace.empty();
}

bool
TrailRenderer::LoadTrace(const TraceComputer &trace_computer,
                         TimeStamp min_time,
                         const WindowProjection &projection) noexcept
{
  const TrailQuery query = MakeTrailQuery(min_time, projection);
  return SyncTrace(trace_computer, query);
}

/**
 * Soft cap on drawable trail samples.  Scaled with short-edge resolution
 * and point size so denser panels may keep a bit more; clamped so weak
 * ARM targets stay bounded regardless of airspeed / zoom.
 */
[[gnu::pure]]
static unsigned
GetTrailPointBudget() noexcept
{
  const unsigned pt = std::max(1u, Layout::PtScale(2));
  const unsigned by_screen = Layout::min_screen_pixels / pt;
  return std::clamp(by_screen, 96u, 384u);
}

TrailQuery
TrailRenderer::MakeTrailQuery(TimeStamp min_time,
                              const WindowProjection &projection,
                              bool keep_all) noexcept
{
  const double map_scale = projection.GetMapScale();
  TrailQuery query;
  query.min_time = min_time.Cast<std::chrono::duration<unsigned>>();
  query.bounds = projection.GetScreenBounds().Scale(TRAIL_BOUNDS_SCALE);
  query.project_location = projection.GetGeoScreenCenter();
  query.keep_all = keep_all;
  if (keep_all) {
    /* No screen-space thinning or soft budget — every stored fix. */
    query.min_distance_m = 0;
    query.point_stride = 1;
    query.max_points = 0;
  } else {
    query.min_distance_m =
      projection.DistancePixelsToMeters(GetTrailSpacingPixels(map_scale));
    query.point_stride = 1;
    query.max_points = GetTrailPointBudget();
  }
  return query;
}

bool
TrailRenderer::TrailDrawFingerprint::operator==(
    const TrailDrawFingerprint &other) const noexcept
{
  return scale_px_per_m == other.scale_px_per_m &&
    color_min == other.color_min &&
    color_max == other.color_max &&
    settings_type == other.settings_type &&
    query_bounds.GetNorthWest() == other.query_bounds.GetNorthWest() &&
    query_bounds.GetSouthEast() == other.query_bounds.GetSouthEast();
}

void
TrailRenderer::InvalidateHistory() noexcept
{
  history.clear();
  history_vario.clear();
  history_valid = false;
  history_min_time = {};
  last_query = {};
  color_scale_valid = false;
}

void
TrailRenderer::InvalidateSegmentCache() noexcept
{
  segment_cache.clear();
  color_scale_valid = false;
#ifdef ENABLE_OPENGL
  full_trail_vertices.clear();
  full_trail_ranges.clear();
  full_trail_vbo.reset();
  full_trail_vbo_capacity = 0;
  full_trail_point_count = 0;
  full_trail_pending_vertex = 0;
  full_trail_smoothing_enabled = false;
  full_trail_reference.SetInvalid();
#endif
}

bool
TrailRenderer::TrailQueryViewEqual(const TrailQuery &a,
                                   const TrailQuery &b) noexcept
{
  if (a.keep_all != b.keep_all)
    return false;

  /* keep_all ignores viewport: pan/zoom must not refilter the store. */
  if (a.keep_all)
    return a.min_distance_m == b.min_distance_m &&
      a.point_stride == b.point_stride &&
      a.max_points == b.max_points;

  return a.min_distance_m == b.min_distance_m &&
    a.point_stride == b.point_stride &&
    a.max_points == b.max_points &&
    a.project_location == b.project_location &&
    a.bounds.GetNorthWest() == b.bounds.GetNorthWest() &&
    a.bounds.GetSouthEast() == b.bounds.GetSouthEast();
}

void
TrailRenderer::RefilterTraceFromHistory(const TrailSpatialFilter &filter) noexcept
{
  FilterTraceByBounds(history, trace, filter);

  merge_vario_samples.clear();
  if (trace.empty())
    return;

  const auto vario_min = trace.front().GetTime();
  merge_vario_samples.reserve(history_vario.size());
  for (const auto &s : history_vario) {
    if (s.time >= vario_min)
      merge_vario_samples.push_back(s);
  }
}

bool
TrailRenderer::SyncTrace(const TraceComputer &trace_computer,
                         const TrailQuery &query) noexcept
{
  Serial append{}, modify{};
  try {
    trace_computer.LockedGetSerials(append, modify);
  } catch (const std::bad_alloc &) {
    InvalidateHistory();
    InvalidateSegmentCache();
    trace.clear();
    merge_vario_samples.clear();
    return false;
  }

  const bool have_history = history_valid;
  const bool modify_ok =
    have_history && modify == history_modify_serial;
  const bool min_time_ok =
    have_history && history_min_time == query.min_time;
  const bool append_ok =
    have_history && append == history_append_serial;
  const bool view_ok =
    have_history && TrailQueryViewEqual(last_query, query);

  if (modify_ok && min_time_ok && append_ok && view_ok) {
    synced_append_serial = append;
    synced_modify_serial = modify;
    return !trace.empty();
  }

  try {
    if (!modify_ok || !min_time_ok) {
      trace_computer.LockedCopyHistory(query.min_time, history,
                                       history_vario, &append, &modify);
      history_append_serial = append;
      history_modify_serial = modify;
      history_min_time = query.min_time;
      history_valid = true;
    } else if (!append_ok) {
      const TracePoint::Time after =
        history.empty() ? TracePoint::Time{} : history.back().GetTime();
      trace_computer.LockedAppendHistoryAfter(after, history, history_vario,
                                              &append, &modify);
      history_append_serial = append;
      history_modify_serial = modify;
    }

    if (query.keep_all) {
      trace = history;
      merge_vario_samples.clear();
      if (!trace.empty()) {
        const auto vario_min = trace.front().GetTime();
        merge_vario_samples.reserve(history_vario.size());
        for (const auto &s : history_vario) {
          if (s.time >= vario_min)
            merge_vario_samples.push_back(s);
        }
      }
    } else {
      const TrailSpatialFilter filter =
        trace_computer.LockedMakeSpatialFilter(query);
      RefilterTraceFromHistory(filter);
    }
  } catch (const std::bad_alloc &) {
    InvalidateHistory();
    InvalidateSegmentCache();
    trace.clear();
    merge_vario_samples.clear();
    return false;
  }

  last_query = query;
  last_query.min_time = query.min_time;
  synced_append_serial = append;
  synced_modify_serial = modify;
  return !trace.empty();
}

unsigned
TrailRenderer::ColorScale::Index(double value) const noexcept
{
  static constexpr unsigned max_index = TrailLook::NUMSNAILCOLORS - 1;

  if (is_altitude) {
    const int idx = int((value - alt_min) * alt_inv_range);
    if (idx <= 0)
      return 0;
    if (unsigned(idx) >= max_index)
      return max_index;
    return unsigned(idx);
  }

  const double cv = value < 0 ? value * neg_inv_min : value * inv_max;
  const int idx =
    int((cv + 1) * (TrailLook::NUMSNAILCOLORS * 0.5));
  if (idx <= 0)
    return 0;
  if (unsigned(idx) >= max_index)
    return max_index;
  return unsigned(idx);
}

TrailRenderer::ColorScale
TrailRenderer::ColorScale::FromMinMax(TrailSettings::Type type,
                                      double value_min,
                                      double value_max) noexcept
{
  ColorScale scale;
  if (type == TrailSettings::Type::ALTITUDE) {
    scale.is_altitude = true;
    scale.alt_min = value_min;
    const double range = value_max - value_min;
    scale.alt_inv_range = range > 0
      ? double(TrailLook::NUMSNAILCOLORS - 1) / range
      : 0.;
  } else {
    scale.neg_inv_min = -1.0 / value_min;
    scale.inv_max = 1.0 / value_max;
  }
  return scale;
}

[[gnu::pure]]
static std::pair<double, double>
GetMinMax(TrailSettings::Type type, const TracePointVector &trace) noexcept
{
  double value_min, value_max;

  if (type == TrailSettings::Type::ALTITUDE) {
    value_max = 1000;
    value_min = 500;

    for (const auto &i : trace) {
      value_max = std::max(i.GetAltitude(), value_max);
      value_min = std::min(i.GetAltitude(), value_min);
    }
  } else {
    value_max = 0.75;
    value_min = -2.0;

    for (const auto &i : trace) {
      value_max = std::max(i.GetVario(), value_max);
      value_min = std::min(i.GetVario(), value_min);
    }

    value_max = std::min(7.5, value_max);
    value_min = std::max(-5.0, value_min);
  }

  return std::make_pair(value_min, value_max);
}

PixelPoint
TrailRenderer::ProjectCachedPathPoint(
    const CachedPathPoint &p,
    const WindowProjection &projection,
    const bool enable_traildrift,
    const GeoPoint &traildrift,
    const TimeStamp drift_now) noexcept
{
  return projection.GeoToScreen(
    DriftGeoPoint(p.geo, p.time, p.drift_factor,
                  enable_traildrift, traildrift, drift_now));
}

void
TrailRenderer::ProjectCachedColourRun(
    const CachedColourRun &run,
    const size_t start_index,
    const WindowProjection &projection,
    const bool enable_traildrift,
    const GeoPoint &traildrift,
    const TimeStamp drift_now,
    BulkPixelPoint *buffer,
    unsigned &n,
    const bool simplify_projected) noexcept
{
  for (size_t i = start_index; i < run.points.size(); ++i)
    AppendFilteredTrailPoint(
      buffer, n,
      ProjectCachedPathPoint(run.points[i], projection,
                             enable_traildrift, traildrift, drift_now),
      simplify_projected);
}

void
TrailRenderer::DrawCachedSegments(Canvas &canvas,
                                  const WindowProjection &projection,
                                  const TrailSettings::Type type,
                                  const bool scaled_trail,
                                  const bool enable_traildrift,
                                  const GeoPoint &traildrift,
                                  const TimeStamp drift_now,
                                  const std::vector<CachedTrailSegment> &segments) noexcept
{
  const bool suppress_sink_lines = IsVarioDotsOnlyMode(type);
  const bool use_ribbon = UseRibbonTrail(type, scaled_trail);
  const bool simplify_projected = IsLowDpiTrail() && use_ribbon;
  static constexpr unsigned null_color_index =
    TrailLook::NUMSNAILCOLORS / 2;

  if (use_ribbon) {
#ifdef ENABLE_OPENGL
    ribbon_vertices.clear();
    ribbon_colors.clear();

    size_t total_pts = 0;
    for (const auto &seg : segments)
      for (const auto &run : seg.colour_runs)
        total_pts += run.points.size();
    /* ~6 verts/segment + join discs; reserve roughly. */
    ribbon_vertices.reserve(total_pts * 8);
    ribbon_colors.reserve(total_pts * 8);

    for (const auto &seg : segments) {
      for (const auto &run : seg.colour_runs) {
        if (run.points.size() < 2)
          continue;

        if (suppress_sink_lines && run.color_index < null_color_index)
          continue;

        auto *dst = Prepare(run.points.size());
        unsigned n = 0;
        ProjectCachedColourRun(run, 0, projection, enable_traildrift,
                               traildrift, drift_now, dst, n,
                               simplify_projected);
        AppendRibbonGeometry(run.color_index, dst, n);
      }
    }

    FlushRibbonBatch();
#else
    for (const auto &seg : segments) {
      for (const auto &run : seg.colour_runs) {
        if (run.points.size() < 2)
          continue;

        if (suppress_sink_lines && run.color_index < null_color_index)
          continue;

        auto *dst = Prepare(run.points.size());
        unsigned n = 0;
        ProjectCachedColourRun(run, 0, projection, enable_traildrift,
                               traildrift, drift_now, dst, n,
                               simplify_projected);

        DrawRibbonPolyline(canvas, run.color_index, dst, n);
      }
    }
#endif

    return;
  }

  size_t max_batch_points = 0;
  size_t current_batch_points = 0;
  unsigned current_batch_color = TrailLook::NUMSNAILCOLORS;
  for (const auto &seg : segments) {
    for (const auto &run : seg.colour_runs) {
      if (run.points.size() < 2)
        continue;

      if (suppress_sink_lines && run.color_index < null_color_index)
        continue;

      if (run.color_index != current_batch_color) {
        max_batch_points = std::max(max_batch_points, current_batch_points);
        current_batch_points = 0;
        current_batch_color = run.color_index;
      }

      current_batch_points += run.points.size();
    }
  }

  max_batch_points = std::max(max_batch_points, current_batch_points);
  if (max_batch_points > 0)
    points.GrowDiscard(max_batch_points);

  unsigned batch_color = TrailLook::NUMSNAILCOLORS;
  unsigned batch_n = 0;

  auto flush_batch = [&]() noexcept {
    if (batch_n < 2)
      return;

    SelectTrailPen(canvas, batch_color, scaled_trail);
    DrawPreparedPolyline(canvas, batch_n);
    batch_n = 0;
  };

  for (const auto &seg : segments) {
    for (const auto &run : seg.colour_runs) {
      if (run.points.size() < 2)
        continue;

      if (suppress_sink_lines && run.color_index < null_color_index) {
        flush_batch();
        continue;
      }

      if (run.color_index != batch_color)
        flush_batch();

      batch_color = run.color_index;

      size_t start = 0;
      if (batch_n > 0 && !run.points.empty()) {
        const PixelPoint junction =
          ProjectCachedPathPoint(run.points.front(), projection,
                                 enable_traildrift, traildrift, drift_now);
        if (junction.x == points[batch_n - 1].x &&
            junction.y == points[batch_n - 1].y)
          start = 1;
      }

      ProjectCachedColourRun(run, start, projection,
                             enable_traildrift, traildrift, drift_now,
                             points.data(), batch_n, simplify_projected);
    }
  }

  flush_batch();
}

#ifdef ENABLE_OPENGL

void
TrailRenderer::AppendFullTrailVertex(unsigned color_index,
                                     FloatPoint2D p) noexcept
{
  if (full_trail_ranges.empty() ||
      full_trail_ranges.back().color_index != color_index) {
    FloatPoint2D junction = p;
    if (!full_trail_vertices.empty())
      junction = full_trail_vertices.back();

    full_trail_ranges.push_back({
      color_index,
      unsigned(full_trail_vertices.size()),
      0,
    });

    /* Always emit the shared endpoint into the new strip.  When the
       next leg starts at the previous end (p == junction), skipping
       that push left the new run with only the far point and dropped
       the connecting segment — a dotted trail. */
    full_trail_vertices.push_back(junction);
    ++full_trail_ranges.back().count;

    if (p.x != junction.x || p.y != junction.y) {
      full_trail_vertices.push_back(p);
      ++full_trail_ranges.back().count;
    }
    return;
  }

  if (full_trail_vertices.empty() ||
      full_trail_vertices.back().x != p.x ||
      full_trail_vertices.back().y != p.y) {
    full_trail_vertices.push_back(p);
    ++full_trail_ranges.back().count;
  }
}

void
TrailRenderer::TruncateFullTrailVertices(size_t new_size) noexcept
{
  if (new_size >= full_trail_vertices.size())
    return;

  while (!full_trail_ranges.empty()) {
    auto &run = full_trail_ranges.back();
    if (run.first >= new_size) {
      full_trail_ranges.pop_back();
      continue;
    }

    const size_t end = size_t(run.first) + run.count;
    if (end > new_size)
      run.count = unsigned(new_size - run.first);
    if (run.count == 0)
      full_trail_ranges.pop_back();
    break;
  }

  full_trail_vertices.resize(new_size);
}

void
TrailRenderer::EmitFullTrailLeg(const ColorScale &color_scale,
                                TrailSettings::Type type,
                                bool use_smoothing,
                                unsigned num_segments,
                                size_t leg_index) noexcept
{
  if (leg_index + 1 >= trace.size() || !full_trail_reference.IsValid())
    return;

  const auto &a = trace[leg_index];
  const auto &b = trace[leg_index + 1];
  const double value = (type == TrailSettings::Type::ALTITUDE)
    ? b.GetAltitude() : b.GetVario();
  const unsigned color_index = color_scale.Index(value);

  if (IsVarioDotsOnlyMode(type) &&
      color_index < TrailLook::NUMSNAILCOLORS / 2)
    return;

  auto rel = [&](const GeoPoint &g) noexcept -> FloatPoint2D {
    return FloatPoint2D{
      float((g.longitude - full_trail_reference.longitude).Native()),
      float((g.latitude - full_trail_reference.latitude).Native()),
    };
  };

  /* Catmull needs g0..g3; bake once when the next fix exists. */
  const bool smooth_leg =
    use_smoothing && num_segments > 0 &&
    leg_index >= 1 && leg_index + 2 < trace.size();

  if (smooth_leg) {
    const GeoPoint g0 = trace[leg_index - 1].GetLocation();
    const GeoPoint g1 = a.GetLocation();
    const GeoPoint g2 = b.GetLocation();
    const GeoPoint g3 = trace[leg_index + 2].GetLocation();
    AppendFullTrailVertex(color_index, rel(g1));
    for (unsigned s = 1; s < num_segments; ++s) {
      const double t = double(s) / double(num_segments);
      AppendFullTrailVertex(color_index,
                            rel(CatmullRomGeo(g0, g1, g2, g3, t)));
    }
    AppendFullTrailVertex(color_index, rel(g2));
  } else {
    AppendFullTrailVertex(color_index, rel(a.GetLocation()));
    AppendFullTrailVertex(color_index, rel(b.GetLocation()));
  }
}

void
TrailRenderer::RebuildFullTrailGeoRuns(const ColorScale &color_scale,
                                       TrailSettings::Type type,
                                       bool use_smoothing,
                                       unsigned num_segments) noexcept
{
  full_trail_vertices.clear();
  full_trail_ranges.clear();
  full_trail_point_count = 0;
  full_trail_pending_vertex = 0;
  if (trace.size() < 2)
    return;

  full_trail_reference = trace.front().GetLocation();

  /* Bake Catmull for every leg that already has a following fix.
     With smoothing, the newest GPS leg stays out of the VBO and is
     drawn live (Catmull with aircraft as g3) until the next fix. */
  for (size_t i = 0; i + 1 < trace.size(); ++i) {
    if (use_smoothing && num_segments > 0 && i + 2 >= trace.size())
      break;

    EmitFullTrailLeg(color_scale, type, use_smoothing, num_segments, i);
  }

  full_trail_pending_vertex = full_trail_vertices.size();
  full_trail_point_count = trace.size();
  UploadFullTrailVBO(0);
}

void
TrailRenderer::AppendFullTrailGeoLegs(const ColorScale &color_scale,
                                      TrailSettings::Type type,
                                      bool use_smoothing,
                                      unsigned num_segments,
                                      size_t from_point) noexcept
{
  if (trace.size() < 2 || from_point == 0 ||
      from_point >= trace.size() || !full_trail_reference.IsValid())
    return;

  size_t upload_from = full_trail_vertices.size();

  if (use_smoothing && num_segments > 0) {
    /* VBO has no provisional tip — only append newly finalizable legs. */
    const size_t first_new =
      from_point >= 2 ? from_point - 2 : 0;
    for (size_t i = first_new; i + 2 < trace.size(); ++i)
      EmitFullTrailLeg(color_scale, type, use_smoothing, num_segments, i);

    full_trail_pending_vertex = full_trail_vertices.size();
    full_trail_point_count = trace.size();
    if (full_trail_vertices.size() != upload_from)
      UploadFullTrailVBO(upload_from);
    return;
  }

  for (size_t i = from_point - 1; i + 1 < trace.size(); ++i)
    EmitFullTrailLeg(color_scale, type, false, 0, i);

  full_trail_pending_vertex = full_trail_vertices.size();
  full_trail_point_count = trace.size();
  if (full_trail_vertices.size() != upload_from)
    UploadFullTrailVBO(upload_from);
}

void
TrailRenderer::UploadFullTrailVBO(size_t from_vertex) noexcept
{
  if (full_trail_vertices.empty()) {
    full_trail_vbo.reset();
    full_trail_vbo_capacity = 0;
    return;
  }

  if (full_trail_vbo == nullptr)
    full_trail_vbo = std::make_unique<GLDynamicArrayBuffer>();

  const size_t n = full_trail_vertices.size();
  const size_t bytes = n * sizeof(FloatPoint2D);

  if (n > full_trail_vbo_capacity || from_vertex == 0) {
    size_t capacity = full_trail_vbo_capacity;
    if (n > capacity) {
      capacity = std::max(n, std::max(size_t(256), capacity * 2));
      full_trail_vbo_capacity = capacity;
    }

    full_trail_vbo->Bind();
    GLDynamicArrayBuffer::Data(GLsizeiptr(full_trail_vbo_capacity *
                                          sizeof(FloatPoint2D)),
                               nullptr);
    GLDynamicArrayBuffer::SubData(0, GLsizeiptr(bytes),
                                  full_trail_vertices.data());
    GLDynamicArrayBuffer::Unbind();
    return;
  }

  if (from_vertex >= n)
    return;

  full_trail_vbo->Bind();
  GLDynamicArrayBuffer::SubData(GLintptr(from_vertex * sizeof(FloatPoint2D)),
                                GLsizeiptr((n - from_vertex) *
                                           sizeof(FloatPoint2D)),
                                full_trail_vertices.data() + from_vertex);
  GLDynamicArrayBuffer::Unbind();
}

void
TrailRenderer::DrawFullTrailGPU(Canvas &canvas,
                                const WindowProjection &projection,
                                TrailSettings::Type type,
                                bool scaled_trail,
                                const ColorScale &color_scale,
                                bool use_smoothing,
                                unsigned num_segments,
                                const NMEAInfo &basic,
                                PixelPoint aircraft_pos,
                                const TrailSettings &settings) noexcept
{
  const bool color_changed =
    full_trail_type != type ||
    full_trail_color_min != (color_scale.is_altitude
                             ? color_scale.alt_min
                             : color_scale.neg_inv_min) ||
    full_trail_color_max != (color_scale.is_altitude
                             ? color_scale.alt_inv_range
                             : color_scale.inv_max);

  const bool modify_changed =
    full_trail_modify_serial != synced_modify_serial;
  const bool append_changed =
    full_trail_append_serial != synced_append_serial;
  /* Turning smoothing on must rebuild so history gets Catmull once;
     turning it off leaves baked curves (append continues straight). */
  const bool smoothing_on =
    use_smoothing != full_trail_smoothing_enabled && use_smoothing;
  const bool can_append =
    !color_changed && !modify_changed && !smoothing_on && append_changed &&
    full_trail_reference.IsValid() &&
    full_trail_point_count > 0 &&
    full_trail_point_count < trace.size() &&
    full_trail_vbo != nullptr;

  if (full_trail_vertices.empty() || !full_trail_reference.IsValid() ||
      modify_changed || color_changed || smoothing_on ||
      (append_changed && !can_append)) {
    RebuildFullTrailGeoRuns(color_scale, type, use_smoothing, num_segments);
  } else if (can_append) {
    AppendFullTrailGeoLegs(color_scale, type, use_smoothing, num_segments,
                           full_trail_point_count);
  }

  full_trail_append_serial = synced_append_serial;
  full_trail_modify_serial = synced_modify_serial;
  full_trail_type = type;
  full_trail_smoothing_enabled = use_smoothing;
  full_trail_color_min = color_scale.is_altitude
    ? color_scale.alt_min : color_scale.neg_inv_min;
  full_trail_color_max = color_scale.is_altitude
    ? color_scale.alt_inv_range : color_scale.inv_max;

  if (!full_trail_reference.IsValid() || full_trail_vertices.empty() ||
      full_trail_vbo == nullptr)
    return;

  OpenGL::solid_shader->Use();
  glUniformMatrix4fv(OpenGL::solid_modelview, 1, GL_FALSE,
                     glm::value_ptr(ToGLM(projection, full_trail_reference)));

  full_trail_vbo->Bind();
  {
    const ScopeVertexPointer vp(GL_FLOAT, nullptr);
    for (const auto &run : full_trail_ranges) {
      if (run.count < 2)
        continue;

      /* SelectTrailPen only stores the Canvas pen; Bind() uploads colour
         and width to the solid shader (same as Canvas::DrawPolyline). */
      SelectTrailPen(canvas, run.color_index, scaled_trail);
      if (scaled_trail)
        look.scaled_trail_pens[run.color_index].Bind();
      else
        look.trail_pens[run.color_index].Bind();
      glDrawArrays(GL_LINE_STRIP, GLint(run.first), GLsizei(run.count));
    }
  }
  GLDynamicArrayBuffer::Unbind();

  const glm::mat4 identity(1);
  glUniformMatrix4fv(OpenGL::solid_modelview, 1, GL_FALSE,
                     glm::value_ptr(identity));

  /* Live tip: with smoothing, Catmull the last GPS leg using the
     aircraft as g3 (not in VBO yet); always bridge last fix → glider. */
  valid_points.clear();
  if (use_smoothing && num_segments > 0 && trace.size() >= 2) {
    const size_t tip_from = trace.size() >= 3 ? trace.size() - 3 : 0;
    for (size_t i = tip_from; i < trace.size(); ++i) {
      const auto &tp = trace[i];
      const double value = (type == TrailSettings::Type::ALTITUDE)
        ? tp.GetAltitude() : tp.GetVario();
      valid_points.push_back({
        projection.GeoToScreen(tp.GetLocation()),
        value,
        tp.GetTime(),
      });
    }

    const size_t tip_first_smooth =
      GetFirstSmoothedPointIndex(valid_points.size());
    DrawOpenLeg(canvas, settings, color_scale, scaled_trail,
                true, num_segments, tip_first_smooth,
                basic, aircraft_pos);
  }

  if (!trace.empty()) {
    valid_points.clear();
    const auto &tp = trace.back();
    const double value = (type == TrailSettings::Type::ALTITUDE)
      ? tp.GetAltitude() : tp.GetVario();
    valid_points.push_back({
      projection.GeoToScreen(tp.GetLocation()),
      value,
      tp.GetTime(),
    });
    DrawOpenLeg(canvas, settings, color_scale, scaled_trail,
                false, 0, valid_points.size(),
                basic, aircraft_pos);
  }
}

#endif /* ENABLE_OPENGL */

void
TrailRenderer::AppendColourRun(std::vector<CachedColourRun> &runs,
                               const unsigned color_index,
                               const CachedPathPoint &pt) noexcept
{
  if (!runs.empty() && runs.back().color_index == color_index) {
    runs.back().points.push_back(pt);
    return;
  }

  std::vector<CachedPathPoint> pts;
  if (!runs.empty())
    pts.push_back(runs.back().points.back());
  pts.push_back(pt);
  runs.push_back({color_index, std::move(pts)});
}

TrailRenderer::CachedPathPoint
TrailRenderer::LerpCachedPathPoint(const GeoPoint &g0, const GeoPoint &g1,
                                   const TracePoint::Time t0,
                                   const TracePoint::Time t1,
                                   const unsigned df0, const unsigned df1,
                                   const double u) noexcept
{
  return {
    g0.Interpolate(g1, u),
    TracePoint::Time((unsigned)(t0.count() * (1. - u) + t1.count() * u)),
    (uint16_t)(df0 * (1. - u) + df1 * u),
  };
}

void
TrailRenderer::BuildDirectColourRuns(
    const TrailPointData &prev_data,
    const TrailPointData &curr_data,
    const GeoPoint &prev_geo,
    const GeoPoint &curr_geo,
    const unsigned prev_drift_factor,
    const unsigned curr_drift_factor,
    const std::vector<PixelPoint> &segment_pts,
    TrailSettings::Type type,
    const ColorScale &color_scale,
    bool use_merge_vario,
    std::vector<CachedColourRun> &runs) noexcept
{
  if (segment_pts.size() < 2)
    return;

  const double piece_count =
    segment_pts.size() > 1
      ? static_cast<double>(segment_pts.size() - 1)
      : 1.0;

  for (size_t j = 0; j + 1 < segment_pts.size(); ++j) {
    const double u = static_cast<double>(j) / piece_count;
    const double interp_value =
      InterpolatePieceValue(type, prev_data.value, curr_data.value,
                            unsigned(j), piece_count, use_merge_vario,
                            vario_breakpoints);

    AppendColourRun(runs, color_scale.Index(interp_value),
                    LerpCachedPathPoint(prev_geo, curr_geo,
                                        prev_data.time, curr_data.time,
                                        prev_drift_factor, curr_drift_factor,
                                        u));
  }

  AppendColourRun(runs, color_scale.Index(curr_data.value),
                  {curr_geo, curr_data.time,
                   (uint16_t)curr_drift_factor});
}

void
TrailRenderer::BuildSmoothColourRuns(
    const GeoPoint &g0, const GeoPoint &g1,
    const GeoPoint &g2, const GeoPoint &g3,
    const TracePoint::Time time1,
    const TracePoint::Time time2,
    const unsigned drift_factor1,
    const unsigned drift_factor2,
    const unsigned num_segments,
    const TrailPointData &prev_data,
    const TrailPointData &curr_data,
    TrailSettings::Type type,
    const ColorScale &color_scale,
    bool use_merge_vario,
    std::vector<CachedColourRun> &runs) noexcept
{
  const double piece_count = static_cast<double>(num_segments);

  AppendColourRun(runs, color_scale.Index(prev_data.value),
                  {g1, time1, (uint16_t)drift_factor1});

  for (unsigned j = 0; j < num_segments; ++j) {
    const unsigned next_piece = j + 1;
    const double t = static_cast<double>(next_piece) / piece_count;
    const GeoPoint next_geo =
      next_piece == num_segments
        ? g2
        : CatmullRomGeo(g0, g1, g2, g3, t);

    const double interp_value =
      InterpolatePieceValue(type, prev_data.value, curr_data.value,
                            j, piece_count, use_merge_vario,
                            vario_breakpoints);

    const CachedPathPoint meta =
      LerpCachedPathPoint(g1, g2, time1, time2,
                          drift_factor1, drift_factor2, t);

    AppendColourRun(runs, color_scale.Index(interp_value),
                    {next_geo, meta.time, meta.drift_factor});
  }
}

void
TrailRenderer::BuildCachedSegment(const WindowProjection &projection,
                                  const size_t leg_index,
                                  TrailSettings::Type type,
                                  const ColorScale &color_scale,
                                  const bool use_smoothing,
                                  const unsigned num_segments,
                                  const size_t first_smoothed_point,
                                  size_t &merge_sample_index,
                                  CachedTrailSegment &dest) noexcept
{
  assert(leg_index + 1 < trace.size());

  const TracePoint &prev_tp = trace[leg_index];
  const TracePoint &curr_tp = trace[leg_index + 1];
  const GeoPoint gp0 = prev_tp.GetLocation();
  const GeoPoint gp1 = curr_tp.GetLocation();
  dest.colour_runs.clear();

  const double prev_value =
    type == TrailSettings::Type::ALTITUDE
      ? prev_tp.GetAltitude() : prev_tp.GetVario();
  const double curr_value =
    type == TrailSettings::Type::ALTITUDE
      ? curr_tp.GetAltitude() : curr_tp.GetVario();

  const bool use_merge_vario =
    type != TrailSettings::Type::ALTITUDE &&
    !merge_vario_samples.empty();

  if (use_merge_vario)
    BuildVarioBreakpoints(prev_tp.GetTime(), prev_value,
                          curr_tp.GetTime(), curr_value,
                          merge_vario_samples, merge_sample_index,
                          vario_breakpoints);
  else
    vario_breakpoints.clear();

  const GeoPoint g0 = leg_index >= 1
    ? trace[leg_index - 1].GetLocation()
    : gp0;
  const GeoPoint g1 = gp0;
  const GeoPoint g2 = gp1;
  const GeoPoint g3 = leg_index + 2 < trace.size()
    ? trace[leg_index + 2].GetLocation()
    : gp1;

  if (use_smoothing && leg_index + 1 > first_smoothed_point) {
    const TrailPointData prev_data{
      PixelPoint{}, prev_value, prev_tp.GetTime()};
    const TrailPointData curr_data{
      PixelPoint{}, curr_value, curr_tp.GetTime()};
    BuildSmoothColourRuns(g0, g1, g2, g3,
                          prev_tp.GetTime(), curr_tp.GetTime(),
                          prev_tp.GetDriftFactor(), curr_tp.GetDriftFactor(),
                          num_segments, prev_data, curr_data, type, color_scale,
                          use_merge_vario, dest.colour_runs);
  } else {
    const PixelPoint p1s = projection.GeoToScreen(gp0);
    const PixelPoint p2s = projection.GeoToScreen(gp1);
    const TrailPointData prev_data{p1s, prev_value, prev_tp.GetTime()};
    const TrailPointData curr_data{p2s, curr_value, curr_tp.GetTime()};
    BuildDirectSegmentPoints(p1s, p2s, vario_breakpoints,
                             use_merge_vario, interpolated);
    BuildDirectColourRuns(prev_data, curr_data, gp0, gp1,
                          prev_tp.GetDriftFactor(), curr_tp.GetDriftFactor(),
                          interpolated, type, color_scale, use_merge_vario,
                          dest.colour_runs);
  }

  MergeAdjacentColourRuns(dest.colour_runs);
}

void
TrailRenderer::UpdateSegmentCache(const WindowProjection &projection,
                                  TrailSettings::Type type,
                                  const ColorScale &color_scale,
                                  const bool use_smoothing,
                                  const unsigned num_segments,
                                  const size_t first_smoothed_point,
                                  const size_t from_leg,
                                  const bool rebuild) noexcept
{
  if (rebuild) {
    segment_cache.clear();
    merge_sample_search_index = 0;
  }

  if (trace.size() < 2)
    return;

  const size_t start_leg = rebuild ? 0 : from_leg;
  if (rebuild)
    segment_cache.reserve(trace.size() - 1);
  else if (from_leg < segment_cache.size()) {
    segment_cache.resize(from_leg);
    merge_sample_search_index =
      from_leg < trace.size()
        ? FindMergeSampleIndexAtOrAfter(trace[from_leg].GetTime(),
                                        merge_vario_samples)
        : merge_vario_samples.size();
  }

  for (size_t leg = start_leg; leg + 1 < trace.size(); ++leg) {
    segment_cache.emplace_back();
    BuildCachedSegment(projection, leg, type, color_scale,
                       use_smoothing, num_segments, first_smoothed_point,
                       merge_sample_search_index, segment_cache.back());
  }
}

void
TrailRenderer::DrawOpenLeg(Canvas &canvas,
                           const TrailSettings &settings,
                           const ColorScale &color_scale,
                           const bool scaled_trail,
                           const bool use_smoothing,
                           const unsigned num_segments,
                           const size_t first_smoothed_point,
                           const NMEAInfo &basic,
                           const PixelPoint aircraft_pos) noexcept
{
  if (valid_points.size() < 1)
    return;

  const TrailPointData &last_data = valid_points.back();

  const bool draw_lines =
    !(last_data.value < 0 && IsVarioDotsOnlyMode(settings.type));

  if (!draw_lines)
    return;

  const bool use_merge_vario =
    settings.type != TrailSettings::Type::ALTITUDE &&
    !merge_vario_samples.empty();

  TrailPointData open_end{aircraft_pos, last_data.value,
                          basic.time.Cast<TracePoint::Time>()};

  if (use_merge_vario)
    BuildVarioBreakpoints(last_data.time, last_data.value,
                          basic.time.Cast<TracePoint::Time>(),
                          last_data.value,
                          merge_vario_samples, merge_sample_search_index,
                          vario_breakpoints);
  else
    vario_breakpoints.clear();

  if (use_smoothing && valid_points.size() > first_smoothed_point &&
      valid_points.size() >= 2) {
    const auto &prev_data = valid_points[valid_points.size() - 2];
    const PixelPoint p0 = valid_points.size() >= 3
      ? valid_points[valid_points.size() - 3].point
      : prev_data.point;
    DrawSmoothTailSegmentInline(canvas, p0, prev_data.point,
                                last_data.point, aircraft_pos,
                                num_segments, last_data, open_end,
                                settings.type, color_scale, scaled_trail,
                                use_merge_vario);
  } else {
    BuildDirectSegmentPoints(last_data.point, aircraft_pos,
                             vario_breakpoints, use_merge_vario,
                             interpolated);
    DrawVarioColouredPolyline(canvas, interpolated, settings.type,
                              last_data, open_end, color_scale,
                              scaled_trail, use_merge_vario,
                              unsigned(interpolated.size() - 1));
  }
}

void
TrailRenderer::Draw(Canvas &canvas, const TraceComputer &trace_computer,
                    const WindowProjection &projection,
                    TimeStamp min_time,
                    bool enable_traildrift, const PixelPoint pos,
                    const NMEAInfo &basic, const DerivedInfo &calculated,
                    const TrailSettings &settings) noexcept
{
  if (settings.length == TrailSettings::Length::OFF)
    return;

  const bool keep_all =
#ifdef ENABLE_OPENGL
    settings.length == TrailSettings::Length::FULL && settings.vbo;
#else
    /* Soft/GDI still use spacing + budget; no GPU project path. */
    false;
#endif
  const TrailQuery query = MakeTrailQuery(min_time, projection, keep_all);
  if (!SyncTrace(trace_computer, query))
    return;

  if (!basic.location_available || !calculated.wind_available)
    enable_traildrift = false;

  GeoPoint traildrift;
  if (enable_traildrift) {
    GeoPoint tp1 = FindLatitudeLongitude(basic.location,
                                         calculated.wind.bearing,
                                         calculated.wind.norm);
    traildrift = basic.location - tp1;
  }

  /* Colour scale from the drawn #trace for local contrast, but freeze it
     across pan/zoom so thinning does not remap climb/altitude colours. */
  const auto trace_minmax = GetMinMax(settings.type, trace);
  const bool rescale =
    !color_scale_valid ||
    settings.type != color_scale_type ||
    synced_append_serial != color_scale_append_serial ||
    synced_modify_serial != color_scale_modify_serial ||
    query.min_time != color_scale_min_time;
  if (rescale) {
    frozen_color_value_min = trace_minmax.first;
    frozen_color_value_max = trace_minmax.second;
    frozen_color_scale =
      ColorScale::FromMinMax(settings.type, frozen_color_value_min,
                             frozen_color_value_max);
    color_scale_valid = true;
    color_scale_type = settings.type;
    color_scale_append_serial = synced_append_serial;
    color_scale_modify_serial = synced_modify_serial;
    color_scale_min_time = query.min_time;
  }
  const ColorScale &color_scale = frozen_color_scale;

  const double map_scale = projection.GetMapScale();
  const bool zoomed_in = map_scale <= TRAIL_ZOOMED_OUT_MAP_SCALE;
  const bool scaled_trail = settings.scaling_enabled && zoomed_in;

  const bool use_smoothing = UseTrailSmoothing(settings.type, map_scale);

#ifdef ENABLE_OPENGL
  /* Unthinned Full trail: geo buffer + GPU projection.  Wind drift
     still uses the CPU path (time-based geo offset). */
  if (keep_all && !enable_traildrift) {
    const unsigned num_segments = use_smoothing ? TRAIL_SMOOTH_SEGMENTS : 0u;

    DrawFullTrailGPU(canvas, projection, settings.type, scaled_trail,
                     color_scale, use_smoothing, num_segments,
                     basic, pos, settings);
    return;
  }
#endif

  const bool append_changed =
    synced_append_serial != cache_append_serial;
  const bool modify_changed =
    synced_modify_serial != cache_modify_serial;
  const size_t leg_count = trace.size() >= 2 ? trace.size() - 1 : 0;

  if (!stable_drift_time.IsDefined() || append_changed || modify_changed ||
      leg_count > segment_cache.size())
    stable_drift_time = basic.time;

  valid_points.clear();
  valid_points.reserve(trace.size());

  for (const auto &i : trace) {
    const GeoPoint gp = TrailGeoPoint(i, enable_traildrift, traildrift,
                                      stable_drift_time);
    const PixelPoint pt = projection.GeoToScreen(gp);
    const double value = (settings.type == TrailSettings::Type::ALTITUDE)
      ? i.GetAltitude() : i.GetVario();
    valid_points.push_back({pt, value, i.GetTime()});
  }

  if (valid_points.empty())
    return;

  const size_t first_smoothed_point =
    use_smoothing ? GetFirstSmoothedPointIndex(valid_points.size())
                  : valid_points.size();
  const unsigned num_segments = use_smoothing ? TRAIL_SMOOTH_SEGMENTS : 0u;

  const TrailDrawFingerprint new_fingerprint{
    projection.GetScale(),
    query.bounds,
    frozen_color_value_min,
    frozen_color_value_max,
    settings.type,
  };

  const bool fingerprint_changed = !(fingerprint == new_fingerprint);

  if (modify_changed || fingerprint_changed)
    UpdateSegmentCache(projection, settings.type, color_scale,
                       use_smoothing, num_segments, first_smoothed_point,
                       0, true);
  else if (leg_count > segment_cache.size()) {
    const size_t rebuild_from =
      use_smoothing && leg_count > MAX_SMOOTHED_TRAIL_POINTS
        ? leg_count - MAX_SMOOTHED_TRAIL_POINTS
        : use_smoothing ? 0 : segment_cache.size();
    UpdateSegmentCache(projection, settings.type, color_scale,
                       use_smoothing, num_segments, first_smoothed_point,
                       rebuild_from, false);
  }
  else if (leg_count < segment_cache.size())
    segment_cache.resize(leg_count);

  cache_append_serial = synced_append_serial;
  cache_modify_serial = synced_modify_serial;
  fingerprint = new_fingerprint;

  for (size_t i = 1; i < valid_points.size(); ++i) {
    const auto &curr_data = valid_points[i];
    const unsigned color_index = color_scale.Index(curr_data.value);

    const bool draw_dots =
      (curr_data.value < 0 &&
       (IsVarioDotsOnlyMode(settings.type) ||
        settings.type == TrailSettings::Type::VARIO_DOTS_AND_LINES ||
        settings.type == TrailSettings::Type::VARIO_EINK)) ||
      (curr_data.value >= 0 &&
       (settings.type == TrailSettings::Type::VARIO_DOTS_AND_LINES ||
        settings.type == TrailSettings::Type::VARIO_EINK));

    if (!draw_dots)
      continue;

    const auto &prev_data = valid_points[i - 1];
    if (curr_data.value < 0) {
      canvas.SelectNullPen();
      canvas.Select(look.trail_brushes[color_index]);
    } else {
      canvas.Select(look.trail_brushes[color_index]);
      canvas.Select(look.trail_pens[color_index]);
    }
    canvas.DrawCircle({(curr_data.point.x + prev_data.point.x) / 2,
                       (curr_data.point.y + prev_data.point.y) / 2},
                      look.trail_widths[color_index]);
  }

  DrawCachedSegments(canvas, projection, settings.type, scaled_trail,
                     enable_traildrift, traildrift, stable_drift_time,
                     segment_cache);
  DrawOpenLeg(canvas, settings, color_scale, scaled_trail,
              use_smoothing, num_segments, first_smoothed_point,
              basic, pos);
}

void
TrailRenderer::Draw(Canvas &canvas, const WindowProjection &projection) noexcept
{
  canvas.Select(look.trace_pen);
  DrawTraceVector(canvas, projection, trace);
}

void
TrailRenderer::Draw(Canvas &canvas, const TraceComputer &trace_computer,
                    const WindowProjection &projection,
                    TimeStamp min_time) noexcept
{
  if (LoadTrace(trace_computer, min_time, projection))
    Draw(canvas, projection);
}

BulkPixelPoint *
TrailRenderer::Prepare(unsigned n) noexcept
{
  points.GrowDiscard(n);
  return points.data();
}

void
TrailRenderer::SelectTrailPen(Canvas &canvas, unsigned color_index,
                              bool scaled_trail) const noexcept
{
  if (scaled_trail)
    canvas.Select(look.scaled_trail_pens[color_index]);
  else
    canvas.Select(look.trail_pens[color_index]);
}

void
TrailRenderer::DrawColourPolyline(Canvas &canvas, unsigned color_index,
                                  TrailSettings::Type type,
                                  bool scaled_trail,
                                  const std::vector<PixelPoint> &pts,
                                  size_t first, size_t last) noexcept
{
  assert(first <= last);
  assert(last < pts.size());

  const unsigned n = unsigned(last - first + 1);
  if (n < 2)
    return;

  const bool simplify_projected = IsLowDpiTrail() &&
    (UseRibbonTrail(type, scaled_trail) || !scaled_trail);
  auto *dst = Prepare(n);
  unsigned filtered_n = 0;
  for (size_t i = first; i <= last; ++i)
    AppendFilteredTrailPoint(dst, filtered_n, pts[i], simplify_projected);

  if (filtered_n < 2)
    return;

  if (UseRibbonTrail(type, scaled_trail)) {
    DrawRibbonPolyline(canvas, color_index, dst, filtered_n);
    return;
  }

  SelectTrailPen(canvas, color_index, scaled_trail);
  DrawPreparedPolyline(canvas, filtered_n);
}

void
TrailRenderer::DrawRibbonPolyline(Canvas &canvas, unsigned color_index,
                                  const BulkPixelPoint *pts,
                                  unsigned n) noexcept
{
  if (n < 2)
    return;

#ifdef ENABLE_OPENGL
  ribbon_vertices.clear();
  ribbon_colors.clear();
  AppendRibbonGeometry(color_index, pts, n);
  FlushRibbonBatch();
  (void)canvas;
#else
  const double half_width =
    std::max(1., GetRibbonWidth(look, color_index) * 0.5);
  const unsigned join_radius = unsigned(std::ceil(half_width));

  canvas.SelectNullPen();
  canvas.Select(look.trail_brushes[color_index]);

  for (unsigned i = 0; i + 1 < n; ++i) {
    const auto &a = pts[i];
    const auto &b = pts[i + 1];
    const double dx = double(b.x - a.x);
    const double dy = double(b.y - a.y);
    const double length = std::hypot(dx, dy);
    if (length <= 0.)
      continue;

    const double ux = dx / length;
    const double uy = dy / length;
    const double nx = -uy * half_width;
    const double ny = ux * half_width;
    const double overlap = 0.5;
    const double ax = double(a.x) - ux * overlap;
    const double ay = double(a.y) - uy * overlap;
    const double bx = double(b.x) + ux * overlap;
    const double by = double(b.y) + uy * overlap;

    BulkPixelPoint polygon[4] = {
      RoundRibbonPoint(ax + nx, ay + ny),
      RoundRibbonPoint(ax - nx, ay - ny),
      RoundRibbonPoint(bx - nx, by - ny),
      RoundRibbonPoint(bx + nx, by + ny),
    };

    canvas.DrawPolygon(polygon, 4);
  }

  if (join_radius > 1) {
    for (unsigned i = 1; i + 1 < n; ++i)
      canvas.DrawCircle({pts[i].x, pts[i].y}, join_radius);
  }
#endif
}

#ifdef ENABLE_OPENGL

void
TrailRenderer::AppendRibbonGeometry(unsigned color_index,
                                    const BulkPixelPoint *pts,
                                    unsigned n) noexcept
{
  if (n < 2)
    return;

  const Color color = look.trail_brushes[color_index].GetColor();
  const double half_width =
    std::max(1., GetRibbonWidth(look, color_index) * 0.5);
  const unsigned join_radius = unsigned(std::ceil(half_width));

  auto push_tri = [&](BulkPixelPoint a, BulkPixelPoint b,
                      BulkPixelPoint c) noexcept {
    ribbon_vertices.push_back(a);
    ribbon_vertices.push_back(b);
    ribbon_vertices.push_back(c);
    ribbon_colors.push_back(color);
    ribbon_colors.push_back(color);
    ribbon_colors.push_back(color);
  };

  for (unsigned i = 0; i + 1 < n; ++i) {
    const auto &a = pts[i];
    const auto &b = pts[i + 1];
    const double dx = double(b.x - a.x);
    const double dy = double(b.y - a.y);
    const double length = std::hypot(dx, dy);
    if (length <= 0.)
      continue;

    const double ux = dx / length;
    const double uy = dy / length;
    const double nx = -uy * half_width;
    const double ny = ux * half_width;
    const double overlap = 0.5;
    const double ax = double(a.x) - ux * overlap;
    const double ay = double(a.y) - uy * overlap;
    const double bx = double(b.x) + ux * overlap;
    const double by = double(b.y) + uy * overlap;

    const BulkPixelPoint p0 = RoundRibbonPoint(ax + nx, ay + ny);
    const BulkPixelPoint p1 = RoundRibbonPoint(ax - nx, ay - ny);
    const BulkPixelPoint p2 = RoundRibbonPoint(bx - nx, by - ny);
    const BulkPixelPoint p3 = RoundRibbonPoint(bx + nx, by + ny);

    push_tri(p0, p1, p2);
    push_tri(p0, p2, p3);
  }

  if (join_radius > 1) {
    constexpr unsigned SIDES = 8;
    for (unsigned i = 1; i + 1 < n; ++i) {
      const BulkPixelPoint center{pts[i].x, pts[i].y};
      BulkPixelPoint prev = RoundRibbonPoint(
        double(center.x) + join_radius,
        double(center.y));
      for (unsigned s = 1; s <= SIDES; ++s) {
        const Angle ang = Angle::FullCircle() * (double(s) / SIDES);
        const BulkPixelPoint cur = RoundRibbonPoint(
          double(center.x) + join_radius * ang.fastcosine(),
          double(center.y) + join_radius * ang.fastsine());
        push_tri(center, prev, cur);
        prev = cur;
      }
    }
  }
}

void
TrailRenderer::FlushRibbonBatch() noexcept
{
  const unsigned n = unsigned(ribbon_vertices.size());
  if (n < 3) {
    ribbon_vertices.clear();
    ribbon_colors.clear();
    return;
  }

  assert(ribbon_colors.size() == ribbon_vertices.size());

  OpenGL::solid_shader->Use();
  const ScopeVertexPointer vp(ribbon_vertices.data());
  const ScopeColorPointer cp(ribbon_colors.data());
  glDrawArrays(GL_TRIANGLES, 0, GLsizei(n));

  ribbon_vertices.clear();
  ribbon_colors.clear();
}

#endif /* ENABLE_OPENGL */

void
TrailRenderer::DrawVarioColouredPolyline(Canvas &canvas,
                                         const std::vector<PixelPoint> &pts,
                                         TrailSettings::Type type,
                                         const TrailPointData &prev_data,
                                         const TrailPointData &curr_data,
                                         const ColorScale &color_scale,
                                         const bool scaled_trail,
                                         const bool use_merge_vario,
                                         const unsigned num_pieces) noexcept
{
  if (pts.size() < 2 || num_pieces == 0)
    return;

  const double piece_count = static_cast<double>(num_pieces);

  size_t run_start = 0;
  unsigned run_color = 0;
  bool run_active = false;

  auto flush_colour_run = [&](size_t run_end) {
    if (!run_active || run_end <= run_start)
      return;
    DrawColourPolyline(canvas, run_color, type, scaled_trail,
                       pts, run_start, run_end);
  };

  for (unsigned j = 0; j < num_pieces; ++j) {
    const unsigned seg_color_index =
      color_scale.Index(InterpolatePieceValue(type, prev_data.value,
                                              curr_data.value, j,
                                              piece_count, use_merge_vario,
                                              vario_breakpoints));

    if (!run_active) {
      run_start = j;
      run_color = seg_color_index;
      run_active = true;
    } else if (seg_color_index != run_color) {
      flush_colour_run(j);
      run_start = j;
      run_color = seg_color_index;
    }
  }

  if (run_active)
    flush_colour_run(pts.size() - 1);
}

void
TrailRenderer::DrawSmoothTailSegmentInline(
    Canvas &canvas,
    const PixelPoint &p0, const PixelPoint &p1,
    const PixelPoint &p2, const PixelPoint &p3,
    unsigned num_segments,
    const TrailPointData &prev_data,
    const TrailPointData &curr_data,
    TrailSettings::Type type,
    const ColorScale &color_scale,
    bool scaled_trail,
    bool use_merge_vario) noexcept
{
  const double piece_count = static_cast<double>(num_segments);

  interpolated.clear();
  interpolated.push_back(p1);

  for (unsigned j = 0; j < num_segments; ++j) {
    const unsigned next_piece = j + 1;
    const double t = static_cast<double>(next_piece) / piece_count;
    interpolated.push_back(
      next_piece == num_segments
        ? p2
        : CatmullRomInterpolate(p0, p1, p2, p3, t));
  }

  DrawVarioColouredPolyline(canvas, interpolated, type,
                            prev_data, curr_data, color_scale,
                            scaled_trail, use_merge_vario, num_segments);
}

void
TrailRenderer::DrawPreparedPolyline(Canvas &canvas, unsigned n) noexcept
{
  assert(points.size() >= n);

  canvas.DrawPolyline(points.data(), n);
}

void
TrailRenderer::DrawPreparedPolygon(Canvas &canvas, unsigned n) noexcept
{
  assert(points.size() >= n);

  canvas.DrawPolygon(points.data(), n);
}

void
TrailRenderer::DrawTraceVector(Canvas &canvas, const Projection &projection,
                               const ContestTraceVector &trace) noexcept
{
  const unsigned n = trace.size();

  std::transform(trace.begin(), trace.end(), Prepare(n), [&projection](const auto &i){
    return projection.GeoToScreen(i.GetLocation());
  });

  DrawPreparedPolyline(canvas, n);
}

void
TrailRenderer::DrawTriangle(Canvas &canvas, const Projection &projection,
                            const ContestTraceVector &trace) noexcept
{
  assert(trace.size() == 5);

  const unsigned start = 1, n = 3;

  auto *p = Prepare(n);

  for (unsigned i = start; i < start + n; ++i)
    *p++ = projection.GeoToScreen(trace[i].GetLocation());

  DrawPreparedPolygon(canvas, n);
}

void
TrailRenderer::DrawTraceVector(Canvas &canvas, const Projection &projection,
                               const TracePointVector &trace) noexcept
{
  const unsigned n = trace.size();

  std::transform(trace.begin(), trace.end(), Prepare(n), [&projection](const auto &i){
    return projection.GeoToScreen(i.GetLocation());
  });

  DrawPreparedPolyline(canvas, n);
}
