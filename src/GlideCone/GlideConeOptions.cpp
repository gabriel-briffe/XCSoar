// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeOptions.hpp"
#include "GlideConeOptionsWorker.hpp"
#include "GlideConeField.hpp"
#include "GlideConeDownward.hpp"
#include "Settings.hpp"
#include "LogFile.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Brush.hpp"
#include "ui/canvas/Pen.hpp"
#include "ui/canvas/Color.hpp"
#include "ui/dim/BulkPoint.hpp"
#include "Look/Colors.hpp"
#include "Look/MapLook.hpp"
#include "Projection/WindowProjection.hpp"
#include "Asset.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Scope.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

struct State {
  std::uint32_t request = 0;
  std::uint32_t done = 0;
  std::chrono::steady_clock::time_point last_at{};
  bool has_last = false;
  unsigned last_ms = 0;
  bool has_duration = false;

  unsigned width = 0, height = 0;
  GeoBounds bounds = GeoBounds::Invalid();
  double cell_x = 0, cell_y = 0;
  std::vector<std::uint8_t> mask;
  /** Descending arrival [m MSL] for option cells; unused elsewhere. */
  std::vector<float> arrival;
  /** Elected parent index per cell (-1 unused); used for pan path. */
  std::vector<int> origin;
  /** 1 where the cell is an upward-cone airport/landable seed. */
  std::vector<std::uint8_t> is_seed;
  /** Option seed cell with highest descending arrival; -1 if none. */
  int best_airport_index = -1;
  int start_index = -1;
  GeoPoint start_location = GeoPoint::Invalid();

  /** Cone floors used by the last full-ratio downward (and pan). */
  std::vector<float> floors;
  GlideConeSettings::OptionsDisplay display =
    GlideConeSettings::OptionsDisplay::MARGIN;
  /**
   * Paint step 0..8 for margin and degraded fills.  Empty when the
   * overlay is a single colour.
   */
  std::vector<std::uint8_t> tone;

  /** Bumped when a GPU job is queued or the mask is cleared. */
  std::uint64_t gpu_generation = 0;
  /** True while a GPU downward job is queued or running. */
  bool gpu_pending = false;

  /** Bumped when a CPU job is queued or the mask is cleared. */
  std::uint64_t cpu_generation = 0;
  /** True while a CPU downward job is queued or running. */
  bool cpu_pending = false;

  /** Pan-mode secondary downward from the probe option cell. */
  int pan_cell = -1;
  std::vector<int> pan_origin;
  std::vector<std::uint8_t> pan_mask;
  int pan_best_airport = -1;
  /** Settle-debounce: restarts while the probe cell keeps changing. */
  int pan_watch_cell = -1;
  std::chrono::steady_clock::time_point pan_since{};
};

/** Same settle window as glide-cone job debounce (heavy grid walk). */
static constexpr std::chrono::milliseconds PAN_DOWNWARD_DEBOUNCE{400};

State state;

[[gnu::pure]]
bool
GeoToCell(GeoPoint p, int &x, int &y) noexcept
{
  if (!p.IsValid() || !state.bounds.IsValid() ||
      state.width == 0 || state.height == 0)
    return false;

  const double width_native = state.bounds.GetWidth().Native();
  const double height_native = state.bounds.GetHeight().Native();
  if (width_native <= 0 || height_native <= 0)
    return false;

  const double fx =
    (p.longitude - state.bounds.GetWest()).Native() / width_native;
  const double fy =
    (state.bounds.GetNorth() - p.latitude).Native() / height_native;
  if (fx < 0 || fx >= 1 || fy < 0 || fy >= 1)
    return false;

  x = std::clamp(int(fx * state.width), 0, int(state.width) - 1);
  y = std::clamp(int(fy * state.height), 0, int(state.height) - 1);
  return true;
}

[[gnu::pure]]
GeoPoint
CellToGeo(int x, int y) noexcept
{
  if (!state.bounds.IsValid() || state.width == 0 || state.height == 0)
    return GeoPoint::Invalid();

  const double fx = (x + 0.5) / double(state.width);
  const double fy = (y + 0.5) / double(state.height);
  return GeoPoint(state.bounds.GetWest() + state.bounds.GetWidth() * fx,
                  state.bounds.GetNorth() - state.bounds.GetHeight() * fy);
}

struct Heap {
  std::vector<int> index;
  std::vector<float> arrival;

  void Push(int i, float a) noexcept {
    index.push_back(i);
    arrival.push_back(a);
    SiftUp(index.size() - 1);
  }

  bool Empty() const noexcept { return index.empty(); }

  std::pair<int, float> Pop() noexcept {
    const int i = index[0];
    const float a = arrival[0];
    index[0] = index.back();
    arrival[0] = arrival.back();
    index.pop_back();
    arrival.pop_back();
    SiftDown(0);
    return {i, a};
  }

  void SiftUp(std::size_t i) noexcept {
    while (i > 0) {
      const std::size_t parent = (i - 1) / 2;
      if (arrival[parent] >= arrival[i])
        break;
      Swap(parent, i);
      i = parent;
    }
  }

  void SiftDown(std::size_t i) noexcept {
    const std::size_t n = index.size();
    while (true) {
      std::size_t best = i;
      const std::size_t left = i * 2 + 1;
      const std::size_t right = left + 1;
      if (left < n && arrival[left] > arrival[best])
        best = left;
      if (right < n && arrival[right] > arrival[best])
        best = right;
      if (best == i)
        break;
      Swap(i, best);
      i = best;
    }
  }

  void Swap(std::size_t a, std::size_t b) noexcept {
    std::swap(index[a], index[b]);
    std::swap(arrival[a], arrival[b]);
  }
};

/**
 * Downward optional area: stored upward-cone altitudes are the floor
 * (reach and LOS).  OPTION marks cells whose arrival clears that floor;
 * the wavefront grows only through those cells (same as the GPU).
 */
constexpr std::uint8_t OPTION = 1;

[[gnu::pure]]
bool
HasConeFloor(const std::vector<float> &floors, std::size_t i,
             float max_alt) noexcept
{
  return i < floors.size() && floors[i] < max_alt;
}

/**
 * Stored upward-cone altitudes only.  Proof-lowered ground floors let
 * options LOS tunnel through ridges (terrain+clearance must still block).
 */
void
BuildFloors(const GlideConeField &field,
            std::vector<float> &floors) noexcept
{
  floors = field.result.altitudes;
}

/**
 * Same as the GLES downward #isInViewToOrigin: extended Bresenham with
 * diagonal corner samples.  A ray cell blocks when the descent from the
 * origin is at or below the *stored* cone floor (no floor = does not
 * block).  Option flags alone do not block.
 */
[[gnu::pure]]
bool
CellBlocksRay(const std::vector<float> &best,
              const std::vector<float> &floors,
              float max_alt,
              unsigned width, unsigned height,
              int cx, int cy, int ox, int oy,
              double cell_x, double cell_y, double ratio) noexcept
{
  if (cx < 0 || cy < 0 || cx >= int(width) || cy >= int(height))
    return true;
  if (cx == ox && cy == oy)
    return false;
  const std::size_t i = std::size_t(cy) * width + cx;
  if (!HasConeFloor(floors, i, max_alt))
    return false;
  const float descent = best[std::size_t(oy) * width + ox] -
    float(std::sqrt((cx - ox) * (cx - ox) * cell_x * cell_x +
                    (cy - oy) * (cy - oy) * cell_y * cell_y) / ratio);
  return floors[i] >= descent;
}

bool
InView(const std::vector<float> &best,
       const std::vector<float> &floors,
       float max_alt,
       unsigned width, unsigned height,
       int x0, int y0, int ox, int oy,
       double cell_x, double cell_y, double ratio) noexcept
{
  if (ox < 0 || oy < 0 || ox >= int(width) || oy >= int(height))
    return false;
  if (x0 == ox && y0 == oy)
    return true;

  const int adx = std::abs(ox - x0);
  const int ady = std::abs(oy - y0);
  int x1 = x0;
  int y1 = y0;
  const int xstep = ox > x1 ? 1 : -1;
  const int ystep = oy > y1 ? 1 : -1;
  const int dx = adx;
  const int dy = ady;
  const int ddy = dy * 2;
  const int ddx = dx * 2;
  int error = dx;
  int errorprev = error;

  if (dx >= dy) {
    for (int s = 0; s < dx; ++s) {
      x1 += xstep;
      error += ddy;
      if (error > ddx) {
        y1 += ystep;
        error -= ddx;
        if (error + errorprev < ddx) {
          if (CellBlocksRay(best, floors, max_alt, width, height,
                            x1, y1 - ystep, ox, oy, cell_x, cell_y, ratio))
            return false;
        } else if (error + errorprev > ddx) {
          if (CellBlocksRay(best, floors, max_alt, width, height,
                            x1 - xstep, y1, ox, oy, cell_x, cell_y, ratio))
            return false;
        }
      }
      if (CellBlocksRay(best, floors, max_alt, width, height,
                        x1, y1, ox, oy, cell_x, cell_y, ratio))
        return false;
      errorprev = error;
    }
  } else {
    for (int s = 0; s < dy; ++s) {
      y1 += ystep;
      error += ddx;
      if (error > ddy) {
        x1 += xstep;
        error -= ddy;
        if (error + errorprev < ddy) {
          if (CellBlocksRay(best, floors, max_alt, width, height,
                            x1 - xstep, y1, ox, oy, cell_x, cell_y, ratio))
            return false;
        } else if (error + errorprev > ddy) {
          if (CellBlocksRay(best, floors, max_alt, width, height,
                            x1, y1 - ystep, ox, oy, cell_x, cell_y, ratio))
            return false;
        }
      }
      if (CellBlocksRay(best, floors, max_alt, width, height,
                        x1, y1, ox, oy, cell_x, cell_y, ratio))
        return false;
      errorprev = error;
    }
  }
  return true;
}

/**
 * Downward options wavefront from (@p gi,@p gj) at @p start_alt.
 * Fills @p best / @p origin / @p flags (OPTION bit; size width*height).
 * Only cells above the cone floor are written; below-floor arrivals
 * stay empty so a better path can still fill them.
 * @return false if the start cell cannot begin an optional area.
 */
bool
PropagateDownward(const GlideConeField &field, int gi, int gj,
                  float start_alt, double ratio,
                  const std::vector<float> &floors,
                  std::vector<float> &best,
                  std::vector<int> &origin,
                  std::vector<std::uint8_t> &flags) noexcept
{
  const unsigned width = field.result.width;
  const unsigned height = field.result.height;
  const std::size_t n = std::size_t(width) * height;
  if (gi < 0 || gj < 0 || gi >= int(width) || gj >= int(height))
    return false;

  best.assign(n, -1.f);
  origin.assign(n, -1);
  flags.assign(n, 0);

  const std::size_t start = std::size_t(gj) * width + gi;
  if (ratio <= 0 || !HasConeFloor(floors, start, field.max_alt) ||
      start_alt <= floors[start])
    return false;

  best[start] = start_alt;
  origin[start] = int(start);
  flags[start] = OPTION;
  Heap heap;
  heap.Push(int(start), start_alt);

  const double cell_x = field.cell_size_x_m > 0 ? field.cell_size_x_m
                                               : field.cell_size_m;
  const double cell_y = field.cell_size_y_m > 0 ? field.cell_size_y_m
                                               : field.cell_size_m;

  while (!heap.Empty()) {
    const auto [index, arrival] = heap.Pop();
    const std::size_t ui = std::size_t(index);
    if (!(flags[ui] & OPTION))
      continue;
    if (arrival < best[ui])
      continue;
    const int x = index % int(width);
    const int y = index / int(width);
    const int from_origin = origin[ui];
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0)
          continue;
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height))
          continue;
        const std::size_t nidx = std::size_t(ny) * width + nx;

        int elected = index;
        if (from_origin >= 0 &&
            InView(best, floors, field.max_alt, width, height, nx, ny,
                   from_origin % int(width),
                   from_origin / int(width),
                   cell_x, cell_y, ratio))
          elected = from_origin;

        if (!(flags[std::size_t(elected)] & OPTION))
          continue;

        const int ox = elected % int(width);
        const int oy = elected / int(width);
        const double dist = std::hypot((nx - ox) * cell_x, (ny - oy) * cell_y);
        const float next = best[std::size_t(elected)] - float(dist / ratio);

        /* No upward-cone value (capped / unreachable): not an Option. */
        if (!HasConeFloor(floors, nidx, field.max_alt))
          continue;
        /* At/below the floor: leave empty (no GC freeze). */
        if (next <= floors[nidx])
          continue;

        if (next <= best[nidx])
          continue;
        best[nidx] = next;
        origin[nidx] = elected;
        flags[nidx] = OPTION;
        heap.Push(int(nidx), next);
      }
    }
  }
  return true;
}

/** Mask and arrival from OPTION flags written by #PropagateDownward. */
void
BuildMask(const std::vector<float> &best,
          const std::vector<std::uint8_t> &flags,
          std::vector<std::uint8_t> &mask,
          std::vector<float> &arrival) noexcept
{
  const std::size_t n = best.size();
  mask.assign(n, 0);
  arrival.assign(n, -1.f);
  for (std::size_t i = 0; i < n; ++i) {
    if (!(flags[i] & OPTION))
      continue;
    mask[i] = 1;
    arrival[i] = best[i];
  }
}

struct DownwardRun {
  std::vector<float> floors;
  std::vector<float> arrival;
  std::vector<int> origin;
  std::vector<std::uint8_t> mask;
  int start_index = -1;
  bool ok = false;
};

bool
RunDownward(const GlideConeField &field, int gi, int gj,
            double start_alt, double ratio, DownwardRun &out) noexcept
{
  out = {};
  const auto seed = field.ResolveOptionsSeed(gi, gj, start_alt, ratio);
  if (seed.kind == GlideConeField::OptionsSeed::Kind::NONE)
    return false;

  BuildFloors(field, out.floors);
  std::vector<float> best;
  std::vector<std::uint8_t> flags;
  if (!PropagateDownward(field, seed.x, seed.y, seed.arrival, ratio,
                         out.floors, best, out.origin, flags))
    return false;

  BuildMask(best, flags, out.mask, out.arrival);
  out.start_index = seed.y * int(field.result.width) + seed.x;
  out.ok = true;
  return true;
}

void
PaintTone(const DownwardRun &full,
          const std::vector<std::uint8_t> *mask10,
          const std::vector<std::uint8_t> *mask20,
          GlideConeSettings::OptionsDisplay display,
          std::vector<std::uint8_t> &tone) noexcept
{
  tone.clear();
  if (display == GlideConeSettings::OptionsDisplay::SOLID)
    return;

  tone.assign(full.mask.size(), 0);
  if (display == GlideConeSettings::OptionsDisplay::DEGRADED) {
    for (std::size_t i = 0; i < full.mask.size(); ++i) {
      if (!full.mask[i])
        continue;
      if (mask20 != nullptr && i < mask20->size() && (*mask20)[i])
        tone[i] = 8;
      else if (mask10 != nullptr && i < mask10->size() && (*mask10)[i])
        tone[i] = 4;
    }
    return;
  }

  float max_margin = 0;
  for (std::size_t i = 0; i < full.mask.size(); ++i) {
    if (!full.mask[i] || i >= full.floors.size())
      continue;
    max_margin = std::max(max_margin, full.arrival[i] - full.floors[i]);
  }
  for (std::size_t i = 0; i < full.mask.size(); ++i) {
    if (!full.mask[i] || i >= full.floors.size())
      continue;
    const float t = max_margin > 0
      ? (full.arrival[i] - full.floors[i]) / max_margin
      : 0;
    tone[i] = std::uint8_t(std::clamp(int(std::lround(t * 8)), 0, 8));
  }
}

[[gnu::pure]]
int
BestAirportIndex(const std::vector<std::uint8_t> &mask,
                 const std::vector<float> &arrival,
                 const std::vector<std::uint8_t> &is_seed) noexcept
{
  int best_i = -1;
  float best_a = -1.f;
  for (std::size_t i = 0; i < mask.size(); ++i) {
    if (!is_seed[i] || !mask[i] || arrival[i] < 0)
      continue;
    if (arrival[i] > best_a) {
      best_a = arrival[i];
      best_i = int(i);
    }
  }
  return best_i;
}

[[gnu::pure]]
unsigned
MaskCount(const std::vector<std::uint8_t> &mask) noexcept
{
  unsigned n = 0;
  for (const std::uint8_t cell : mask)
    if (cell != 0)
      ++n;
  return n;
}

bool
FillPass(const GlideConeField &field, int gi, int gj, double start_alt,
         double ratio, GlideConeDownwardPass &pass) noexcept
{
  const auto seed = field.ResolveOptionsSeed(gi, gj, start_alt, ratio);
  if (seed.kind == GlideConeField::OptionsSeed::Kind::NONE)
    return false;
  pass.ratio = ratio;
  pass.gi = seed.x;
  pass.gj = seed.y;
  pass.start_alt = seed.arrival;
  BuildFloors(field, pass.floors);
  return true;
}

bool
FillGpuJob(const GlideConeField &field, double start_alt,
           GeoPoint start_location, int gi, int gj,
           const GlideConeSettings &settings,
           GlideConeDownwardJob &job) noexcept
{
  job = {};
  const double ratio = field.glide_ratio > 0 ? field.glide_ratio : 1;
  GlideConeDownwardPass primary;
  if (!FillPass(field, gi, gj, start_alt, ratio, primary)) {
    LogFmt("GlideCone options: no seed {},{} alt={:.0f} ld={:.1f}",
           gi, gj, start_alt, ratio);
    return false;
  }
  job.passes.push_back(std::move(primary));
  if (settings.options_display ==
      GlideConeSettings::OptionsDisplay::DEGRADED) {
    GlideConeDownwardPass ten, twenty;
    if (FillPass(field, gi, gj, start_alt, ratio * 0.9, ten))
      job.passes.push_back(std::move(ten));
    if (FillPass(field, gi, gj, start_alt, ratio * 0.8, twenty))
      job.passes.push_back(std::move(twenty));
  }
  job.width = field.result.width;
  job.height = field.result.height;
  job.cell_x = field.cell_size_x_m > 0 ? field.cell_size_x_m
                                      : field.cell_size_m;
  job.cell_y = field.cell_size_y_m > 0 ? field.cell_size_y_m
                                      : field.cell_size_m;
  job.max_alt = field.max_alt;
  job.bounds = field.bounds;
  job.start_location = start_location;
  job.aircraft_gi = gi;
  job.aircraft_gj = gj;
  job.path_distance_m = 0;
  job.margin_m = 0;
  if (start_location.IsValid()) {
    if (const auto path = field.PathDistance(start_location))
      job.path_distance_m = *path;
    if (const auto proof =
          field.RidgeSoaringProofGlideConeAltitude(start_location))
      job.margin_m = std::max(0.0, start_alt - *proof);
  }
  job.iteration_cap = settings.iteration_cap > 0 ? settings.iteration_cap
                                                : 2000u;
  job.display = settings.options_display;
  return true;
}

} // namespace

void
GlideConeOptions::RequestOnce() noexcept
{
  ++state.request;
}

void
GlideConeOptions::Clear() noexcept
{
  state.mask.clear();
  state.arrival.clear();
  state.origin.clear();
  state.is_seed.clear();
  state.floors.clear();
  state.tone.clear();
  state.best_airport_index = -1;
  state.start_index = -1;
  state.start_location = GeoPoint::Invalid();
  state.width = state.height = 0;
  state.bounds = GeoBounds::Invalid();
  state.pan_cell = -1;
  state.pan_origin.clear();
  state.pan_mask.clear();
  state.pan_best_airport = -1;
  state.pan_watch_cell = -1;
  state.done = state.request;
  ++state.gpu_generation;
  state.gpu_pending = false;
  ++state.cpu_generation;
  state.cpu_pending = false;
}

std::optional<double>
GlideConeOptions::QueryArrivalAltitude(GeoPoint location) noexcept
{
  if (!location.IsValid() || state.mask.empty() ||
      state.arrival.size() != state.mask.size())
    return std::nullopt;

  int x, y;
  if (!GeoToCell(location, x, y))
    return std::nullopt;

  const std::size_t i = std::size_t(y) * state.width + x;
  if (!state.mask[i] || state.arrival[i] < 0)
    return std::nullopt;
  return double(state.arrival[i]);
}

std::optional<unsigned>
GlideConeOptions::LastComputeMs() noexcept
{
  if (!state.has_duration)
    return std::nullopt;
  return state.last_ms;
}

void
GlideConeOptions::Update(const GlideConeField &field, double start_alt,
                         GeoPoint start_location, int gi, int gj,
                         const GlideConeSettings &settings,
                         GlideConeDownwardJob *gpu_job,
                         std::unique_ptr<GlideConeOptionsCpuJob> *cpu_job_out) noexcept
{
  if (settings.options_mode == GlideConeSettings::OptionsMode::OFF) {
    Clear();
    return;
  }
  if (!field.IsValid() || !std::isfinite(start_alt))
    return;

  const auto now = std::chrono::steady_clock::now();
  const bool once = settings.options_mode == GlideConeSettings::OptionsMode::ONCE;
  const bool display_changed = settings.options_display != state.display;
  /* Never queue a second job while one is in flight — that cancels the
     running pass and loops forever with empty masks. */
  if (state.gpu_pending && gpu_job != nullptr && !display_changed)
    return;
  if (state.cpu_pending && cpu_job_out != nullptr && !display_changed)
    return;
  if (once) {
    /* Mask stays empty until Apply*; treat pending/has_last as "started". */
    if (state.request == state.done && !display_changed &&
        (!state.mask.empty() || state.gpu_pending || state.cpu_pending ||
         state.has_last))
      return;
    if (state.request == 0)
      return;
  } else {
    const auto interval = std::chrono::seconds(
      std::clamp(settings.options_routine_s, 1u, 60u));
    if (state.has_last && now - state.last_at < interval &&
        state.request == state.done && !display_changed)
      return;
  }

  const unsigned width = field.result.width;
  const unsigned height = field.result.height;
  if (gi < 0 || gj < 0 || gi >= int(width) || gj >= int(height))
    return;

  if (gpu_job != nullptr) {
    if (!FillGpuJob(field, start_alt, start_location, gi, gj, settings,
                    *gpu_job)) {
      /* Below the cone with no escape seed, or no cone floor. */
      Clear();
      state.display = settings.options_display;
      state.has_last = true;
      state.last_at = std::chrono::steady_clock::now();
    }
    return;
  }

  /* Cheap seed check before copying the field to the worker. */
  const double ratio = field.glide_ratio > 0 ? field.glide_ratio : 1;
  if (field.ResolveOptionsSeed(gi, gj, start_alt, ratio).kind ==
      GlideConeField::OptionsSeed::Kind::NONE) {
    LogFmt("GlideCone options: cpu no seed {},{} alt={:.0f}",
           gi, gj, start_alt);
    Clear();
    state.display = settings.options_display;
    state.has_last = true;
    state.last_at = std::chrono::steady_clock::now();
    return;
  }

  if (cpu_job_out != nullptr) {
    auto job = std::make_unique<GlideConeOptionsCpuJob>();
    job->field = field;
    job->field.contour_lines.clear();
    job->start_alt = start_alt;
    job->start_location = start_location;
    job->gi = gi;
    job->gj = gj;
    job->display = settings.options_display;
    ++state.cpu_generation;
    job->generation = state.cpu_generation;
    state.display = settings.options_display;
    state.cpu_pending = true;
    state.has_last = true;
    state.last_at = now;
    state.done = state.request;
    *cpu_job_out = std::move(job);
    return;
  }

  /* Sync fallback when no worker out-param (should not happen in UI). */
  GlideConeOptionsCpuJob job;
  job.generation = ++state.cpu_generation;
  job.field = field;
  job.start_alt = start_alt;
  job.start_location = start_location;
  job.gi = gi;
  job.gj = gj;
  job.display = settings.options_display;
  state.cpu_pending = true;
  GlideConeOptionsCpuReady ready;
  ComputeCpu(job, ready);
  ApplyCpu(field, std::move(ready), settings);
}

void
GlideConeOptions::ComputeCpu(const GlideConeOptionsCpuJob &job,
                             GlideConeOptionsCpuReady &out) noexcept
{
  out = {};
  out.generation = job.generation;
  out.display = job.display;
  out.aircraft_gi = job.gi;
  out.aircraft_gj = job.gj;
  out.start_location = job.start_location;

  const auto t0 = std::chrono::steady_clock::now();
  const auto &field = job.field;
  if (!field.IsValid())
    return;

  const double ratio = field.glide_ratio > 0 ? field.glide_ratio : 1;
  DownwardRun full;
  if (!RunDownward(field, job.gi, job.gj, job.start_alt, ratio, full)) {
    LogFmt("GlideCone options: cpu no seed {},{} alt={:.0f}",
           job.gi, job.gj, job.start_alt);
    out.compute_ms = unsigned(std::chrono::duration_cast<
      std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count());
    return;
  }

  if (job.display == GlideConeSettings::OptionsDisplay::DEGRADED) {
    DownwardRun ten, twenty;
    if (RunDownward(field, job.gi, job.gj, job.start_alt, ratio * 0.9, ten))
      out.mask10 = std::move(ten.mask);
    if (RunDownward(field, job.gi, job.gj, job.start_alt, ratio * 0.8, twenty))
      out.mask20 = std::move(twenty.mask);
  }

  out.ok = true;
  out.width = field.result.width;
  out.height = field.result.height;
  out.bounds = field.bounds;
  out.cell_x = field.cell_size_x_m > 0 ? field.cell_size_x_m
                                       : field.cell_size_m;
  out.cell_y = field.cell_size_y_m > 0 ? field.cell_size_y_m
                                       : field.cell_size_m;
  out.start_index = full.start_index;
  out.mask = std::move(full.mask);
  out.arrival = std::move(full.arrival);
  out.origin = std::move(full.origin);
  out.floors = std::move(full.floors);
  out.compute_ms = unsigned(std::chrono::duration_cast<
    std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count());
  LogFmt("GlideCone options: cpu {}x{} mask={} {}ms",
         out.width, out.height, MaskCount(out.mask), out.compute_ms);
}

void
GlideConeOptions::ApplyCpu(const GlideConeField &field,
                           GlideConeOptionsCpuReady &&ready,
                           const GlideConeSettings &settings) noexcept
{
  if (ready.generation != state.cpu_generation) {
    LogFmt("GlideCone options: cpu reject stale gen={}/{}",
           ready.generation, state.cpu_generation);
    return;
  }

  state.cpu_pending = false;
  if (!ready.ok) {
    const auto display = ready.display;
    const unsigned ms = ready.compute_ms;
    Clear();
    state.display = display;
    state.has_last = true;
    state.last_at = std::chrono::steady_clock::now();
    state.last_ms = ms;
    state.has_duration = true;
    return;
  }

  if (ready.width != field.result.width ||
      ready.height != field.result.height ||
      ready.arrival.size() != ready.mask.size()) {
    LogFmt("GlideCone options: cpu reject grid={}/{} arrival={}",
           ready.width, field.result.width, ready.arrival.size());
    AbandonCpu();
    return;
  }

  DownwardRun full;
  full.mask = std::move(ready.mask);
  full.arrival = std::move(ready.arrival);
  full.origin = std::move(ready.origin);
  full.floors = std::move(ready.floors);
  full.start_index = ready.start_index;
  full.ok = true;

  const auto *mask10 = ready.mask10.empty() ? nullptr : &ready.mask10;
  const auto *mask20 = ready.mask20.empty() ? nullptr : &ready.mask20;
  PaintTone(full, mask10, mask20, ready.display, state.tone);
  state.mask = std::move(full.mask);
  state.arrival = std::move(full.arrival);
  state.origin = std::move(full.origin);
  state.floors = std::move(full.floors);
  state.display = ready.display;
  const unsigned width = ready.width;
  const unsigned height = ready.height;
  state.is_seed.assign(std::size_t(width) * height, 0);
  for (const auto &s : field.seeds) {
    if (s.x < 0 || s.y < 0 || s.x >= int(width) || s.y >= int(height))
      continue;
    state.is_seed[std::size_t(s.y) * width + s.x] = 1;
  }
  state.best_airport_index =
    BestAirportIndex(state.mask, state.arrival, state.is_seed);
  state.start_index = full.start_index;
  state.width = width;
  state.height = height;
  state.bounds = ready.bounds.IsValid() ? ready.bounds : field.bounds;
  state.cell_x = ready.cell_x;
  state.cell_y = ready.cell_y;
  const int sx = full.start_index >= 0 ? full.start_index % int(width) : -1;
  const int sy = full.start_index >= 0 ? full.start_index / int(width) : -1;
  state.start_location =
    (sx == ready.aircraft_gi && sy == ready.aircraft_gj &&
     ready.start_location.IsValid())
    ? ready.start_location
    : field.CellToGeo(sx, sy);
  state.pan_cell = -1;
  state.pan_origin.clear();
  state.pan_mask.clear();
  state.pan_best_airport = -1;
  state.pan_watch_cell = -1;
  state.last_ms = ready.compute_ms;
  state.has_duration = true;
  state.has_last = true;
  state.last_at = std::chrono::steady_clock::now();
  (void)settings;
}

void
GlideConeOptions::ApplyGpu(const GlideConeField &field,
                           const GlideConeDownwardReady &ready,
                           const GlideConeSettings &settings) noexcept
{
  if (!ready.ok || ready.generation != state.gpu_generation ||
      ready.width != field.result.width ||
      ready.height != field.result.height ||
      ready.arrival.size() != ready.mask.size()) {
    LogFmt("GlideCone options: gpu reject ok={} gen={}/{} grid={}/{} "
           "arrival={}",
           ready.ok, ready.generation, state.gpu_generation,
           ready.width, field.result.width, ready.arrival.size());
    state.gpu_pending = false;
    return;
  }

  DownwardRun full;
  full.mask = ready.mask;
  full.arrival = ready.arrival;
  full.origin = ready.origin;
  full.floors = ready.floors;
  full.start_index = ready.start_index;
  full.ok = true;

  const auto *mask10 = ready.mask10.empty() ? nullptr : &ready.mask10;
  const auto *mask20 = ready.mask20.empty() ? nullptr : &ready.mask20;
  PaintTone(full, mask10, mask20, ready.display, state.tone);
  state.mask = std::move(full.mask);
  state.arrival = std::move(full.arrival);
  state.origin = std::move(full.origin);
  state.floors = std::move(full.floors);
  state.display = ready.display;
  const unsigned width = ready.width;
  const unsigned height = ready.height;
  state.is_seed.assign(std::size_t(width) * height, 0);
  for (const auto &s : field.seeds) {
    if (s.x < 0 || s.y < 0 || s.x >= int(width) || s.y >= int(height))
      continue;
    state.is_seed[std::size_t(s.y) * width + s.x] = 1;
  }
  state.best_airport_index =
    BestAirportIndex(state.mask, state.arrival, state.is_seed);
  state.start_index = full.start_index;
  state.width = width;
  state.height = height;
  state.bounds = ready.bounds.IsValid() ? ready.bounds : field.bounds;
  state.cell_x = field.cell_size_x_m > 0 ? field.cell_size_x_m
                                         : field.cell_size_m;
  state.cell_y = field.cell_size_y_m > 0 ? field.cell_size_y_m
                                         : field.cell_size_m;
  const int sx = full.start_index >= 0 ? full.start_index % int(width) : -1;
  const int sy = full.start_index >= 0 ? full.start_index / int(width) : -1;
  state.start_location =
    (sx == ready.aircraft_gi && sy == ready.aircraft_gj &&
     ready.start_location.IsValid())
    ? ready.start_location
    : field.CellToGeo(sx, sy);
  state.pan_cell = -1;
  state.pan_origin.clear();
  state.pan_mask.clear();
  state.pan_best_airport = -1;
  state.pan_watch_cell = -1;
  const auto now = std::chrono::steady_clock::now();
  state.last_ms = unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(
    now - state.last_at).count());
  state.has_duration = true;
  state.gpu_pending = false;
  state.has_last = true;
  state.last_at = now;
  LogFmt("GlideCone options: gpu apply {}x{} mask={} iter={} {}ms",
         width, height, MaskCount(state.mask), ready.iterations,
         state.last_ms);
  (void)settings;
}

void
GlideConeOptions::NoteGpuQueued(GlideConeDownwardJob &job) noexcept
{
  ++state.gpu_generation;
  job.generation = state.gpu_generation;
  state.display = job.display;
  state.gpu_pending = true;
  state.cpu_pending = false;
  state.has_last = true;
  state.last_at = std::chrono::steady_clock::now();
  state.done = state.request;
}

void
GlideConeOptions::NoteCpuQueued() noexcept
{
  state.cpu_pending = true;
  state.gpu_pending = false;
  state.has_last = true;
  state.last_at = std::chrono::steady_clock::now();
  state.done = state.request;
}

void
GlideConeOptions::AbandonGpu() noexcept
{
  ++state.gpu_generation;
  state.gpu_pending = false;
  /* Keep the routine cooldown so a failed/cancelled job does not
     re-queue on every map frame. */
  state.has_last = true;
  state.last_at = std::chrono::steady_clock::now();
}

void
GlideConeOptions::AbandonCpu() noexcept
{
  ++state.cpu_generation;
  state.cpu_pending = false;
  state.has_last = true;
  state.last_at = std::chrono::steady_clock::now();
}

void
GlideConeOptions::Draw(Canvas &canvas, const WindowProjection &projection,
                       const GlideConeSettings &settings) noexcept
{
  if (settings.options_mode == GlideConeSettings::OptionsMode::OFF ||
      state.mask.empty() || !state.bounds.IsValid())
    return;

  unsigned opacity = std::clamp(settings.options_opacity, 20u, 100u);
  const std::uint8_t alpha = std::uint8_t(opacity * 255 / 100);
  const bool flat = !HasColors() || IsDithered() ||
    settings.options_display == GlideConeSettings::OptionsDisplay::SOLID ||
    state.tone.size() != state.mask.size();
  const Color flat_color = flat && HasColors() && !IsDithered()
    ? Color(COLOR_GLIDE_CONE.Red(), COLOR_GLIDE_CONE.Green(),
            COLOR_GLIDE_CONE.Blue(), alpha)
    : COLOR_BLACK;

  Brush brushes[9];
  if (!flat) {
    for (unsigned step = 0; step < 9; ++step) {
      const Color rgb = step <= 4
        ? MixColors(COLOR_RED, COLOR_GLIDE_CONE_MARGIN_MID,
                    std::uint8_t(255 - step * 255 / 4))
        : MixColors(COLOR_GLIDE_CONE_MARGIN_MID, COLOR_LIGHT_GREEN,
                    std::uint8_t(255 - (step - 4) * 255 / 4));
      brushes[step] = Brush(Color(rgb.Red(), rgb.Green(), rgb.Blue(), alpha));
    }
  }

  canvas.SelectNullPen();
  canvas.Select(flat ? Brush(flat_color) : brushes[0]);

#ifdef ENABLE_OPENGL
  const ScopeAlphaBlend alpha_blend;
#endif

  const PixelRect screen = projection.GetScreenRect();
  for (unsigned y = 0; y < state.height; ++y) {
    for (unsigned x = 0; x < state.width; ++x) {
      if (!state.mask[std::size_t(y) * state.width + x])
        continue;
      if (!flat)
        canvas.Select(brushes[state.tone[std::size_t(y) * state.width + x]]);
      const GeoPoint center = state.bounds.GetCenter();
      (void)center;
      const double west = state.bounds.GetWest().Degrees() +
        (double(x) / state.width) * state.bounds.GetWidth().Degrees();
      const double east = state.bounds.GetWest().Degrees() +
        (double(x + 1) / state.width) * state.bounds.GetWidth().Degrees();
      const double north = state.bounds.GetNorth().Degrees() -
        (double(y) / state.height) * state.bounds.GetHeight().Degrees();
      const double south = state.bounds.GetNorth().Degrees() -
        (double(y + 1) / state.height) * state.bounds.GetHeight().Degrees();
      const GeoPoint corners[4] = {
        GeoPoint(Angle::Degrees(west), Angle::Degrees(north)),
        GeoPoint(Angle::Degrees(east), Angle::Degrees(north)),
        GeoPoint(Angle::Degrees(east), Angle::Degrees(south)),
        GeoPoint(Angle::Degrees(west), Angle::Degrees(south)),
      };
      BulkPixelPoint pts[4];
      bool visible = false;
      for (unsigned i = 0; i < 4; ++i) {
        pts[i] = projection.GeoToScreen(corners[i]);
        if (pts[i].x >= screen.left && pts[i].x <= screen.right &&
            pts[i].y >= screen.top && pts[i].y <= screen.bottom)
          visible = true;
      }
      if (visible)
        canvas.DrawPolygon(pts, 4);
    }
  }
}

/**
 * Polyline from @p start_location (@p start_index) to @p end_location
 * (@p end_index) following @p origin.  @p reachable must be true for
 * the end cell.
 */
bool
BuildOriginPath(const std::vector<int> &origin,
                const std::vector<std::uint8_t> &reachable,
                int start_index, GeoPoint start_location,
                int end_index, GeoPoint end_location,
                const WindowProjection &projection,
                std::vector<BulkPixelPoint> &pts) noexcept
{
  pts.clear();
  if (end_index < 0 || start_index < 0 || !end_location.IsValid() ||
      !start_location.IsValid() || origin.empty() ||
      origin.size() != reachable.size() ||
      !reachable[std::size_t(end_index)])
    return false;

  std::vector<int> cells;
  cells.reserve(64);
  int cur = end_index;
  const unsigned max_steps = (state.width + state.height) * 2;
  for (unsigned step = 0; step < max_steps; ++step) {
    cells.push_back(cur);
    if (cur == start_index)
      break;
    if (cur < 0 || std::size_t(cur) >= origin.size())
      return false;
    const int parent = origin[std::size_t(cur)];
    if (parent < 0 || parent == cur)
      return false;
    cur = parent;
  }
  if (cells.empty() || cells.back() != start_index)
    return false;

  std::reverse(cells.begin(), cells.end());

  pts.reserve(cells.size());
  pts.push_back(projection.GeoToScreen(start_location));
  for (std::size_t i = 1; i + 1 < cells.size(); ++i) {
    const int cx = cells[i] % int(state.width);
    const int cy = cells[i] / int(state.width);
    const GeoPoint geo = CellToGeo(cx, cy);
    if (!geo.IsValid())
      return false;
    pts.push_back(projection.GeoToScreen(geo));
  }
  pts.push_back(projection.GeoToScreen(end_location));
  return pts.size() >= 2;
}

void
StrokePath(Canvas &canvas, const std::vector<BulkPixelPoint> &pts,
           const Pen &border, const Pen &stroke) noexcept
{
  canvas.Select(border);
  canvas.DrawPolyline(pts.data(), unsigned(pts.size()));
  canvas.Select(stroke);
  canvas.DrawPolyline(pts.data(), unsigned(pts.size()));
}

/**
 * Ensure a secondary downward from @p pan_cell at its option altitude.
 * Settle-debounced: the probe cell must stay put for
 * #PAN_DOWNWARD_DEBOUNCE before the heavy walk runs.
 */
bool
EnsurePanAirportPath(const GlideConeField &field, int pan_cell) noexcept
{
  if (pan_cell < 0 || std::size_t(pan_cell) >= state.mask.size() ||
      !state.mask[std::size_t(pan_cell)])
    return false;

  if (state.pan_cell == pan_cell && !state.pan_origin.empty())
    return state.pan_best_airport >= 0;

  const auto now = std::chrono::steady_clock::now();
  if (state.pan_watch_cell != pan_cell) {
    /* Probe moved: restart settle timer and drop a stale green path. */
    state.pan_watch_cell = pan_cell;
    state.pan_since = now;
    state.pan_cell = -1;
    state.pan_origin.clear();
    state.pan_mask.clear();
    state.pan_best_airport = -1;
    return false;
  }

  if (now - state.pan_since < PAN_DOWNWARD_DEBOUNCE)
    return false;

  state.pan_cell = pan_cell;
  state.pan_origin.clear();
  state.pan_mask.clear();
  state.pan_best_airport = -1;

  const int gx = pan_cell % int(state.width);
  const int gy = pan_cell / int(state.width);
  const float start_alt = state.arrival[std::size_t(pan_cell)];
  std::vector<float> best;
  std::vector<std::uint8_t> flags;
  std::vector<float> arrival;
  if (state.floors.size() != state.mask.size() ||
      !PropagateDownward(field, gx, gy, start_alt, field.glide_ratio,
                         state.floors, best, state.pan_origin, flags))
    return false;

  BuildMask(best, flags, state.pan_mask, arrival);
  state.pan_best_airport =
    BestAirportIndex(state.pan_mask, arrival, state.is_seed);
  return state.pan_best_airport >= 0;
}

bool
GlideConeOptions::DrawBestAirportPath(Canvas &canvas,
                                      const WindowProjection &projection,
                                      const MapLook &look) noexcept
{
  if (state.best_airport_index < 0)
    return false;

  const int end = state.best_airport_index;
  const int x = end % int(state.width);
  const int y = end / int(state.width);
  const GeoPoint end_geo = CellToGeo(x, y);
  std::vector<BulkPixelPoint> pts;
  if (!BuildOriginPath(state.origin, state.mask,
                       state.start_index, state.start_location,
                       end, end_geo, projection, pts))
    return false;

  StrokePath(canvas, pts, look.glide_cone_border_pen,
             look.glide_cone_options_pen);
  return true;
}

bool
GlideConeOptions::DrawPanPaths(Canvas &canvas,
                               const WindowProjection &projection,
                               GeoPoint pan_probe,
                               const GlideConeField &field,
                               const MapLook &look) noexcept
{
  if (!pan_probe.IsValid() || state.mask.empty() ||
      state.origin.size() != state.mask.size() ||
      state.start_index < 0 || !state.start_location.IsValid())
    return false;

  int x, y;
  if (!GeoToCell(pan_probe, x, y))
    return false;

  const int pan_cell = y * int(state.width) + x;
  if (!state.mask[std::size_t(pan_cell)])
    return false;

  /* Pink: aircraft → option cell under the probe. */
  std::vector<BulkPixelPoint> pink;
  if (!BuildOriginPath(state.origin, state.mask,
                       state.start_index, state.start_location,
                       pan_cell, pan_probe, projection, pink))
    return false;
  StrokePath(canvas, pink, look.glide_cone_border_pen, look.glide_cone_pen);

  /* Green: fresh downward from that cell to the best airport. */
  if (!EnsurePanAirportPath(field, pan_cell))
    return true;

  const int airport = state.pan_best_airport;
  const int ax = airport % int(state.width);
  const int ay = airport / int(state.width);
  const GeoPoint airport_geo = CellToGeo(ax, ay);
  std::vector<BulkPixelPoint> green;
  if (BuildOriginPath(state.pan_origin, state.pan_mask,
                      pan_cell, pan_probe,
                      airport, airport_geo, projection, green))
    StrokePath(canvas, green, look.glide_cone_border_pen,
               look.glide_cone_options_pen);
  return true;
}
