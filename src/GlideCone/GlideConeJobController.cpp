// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeJobController.hpp"
#include "GlideConeCompute.hpp"
#include "GlideConeField.hpp"
#include "GlideConeOverlay.hpp"
#include "Computer/Settings.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "Message.hpp"
#include "Language/Language.hpp"
#include "LogFile.hpp"

#include <cmath>
#include <functional>
#include <utility>
#include <vector>

/** Combined-mode recompute threshold as a fraction of the window half-width. */
static constexpr double GLIDE_CONE_MAX_OFFSET_FROM_CENTER = 0.25;

/** Single-mode recompute threshold when the target moves [m]. */
static constexpr double GLIDE_CONE_SEED_EPSILON_M = 50;

/** Debounce for parameter / waypoint-list changes. */
static constexpr std::chrono::milliseconds GLIDE_CONE_DEBOUNCE{400};

[[gnu::pure]]
static const char *
ModeName(GlideConeSettings::Mode mode) noexcept
{
  switch (mode) {
  case GlideConeSettings::Mode::OFF:
    return "off";
  case GlideConeSettings::Mode::SINGLE:
    return "single";
  case GlideConeSettings::Mode::COMBINED:
    return "combined";
  }
  return "?";
}

[[gnu::pure]]
static std::size_t
SettingsSignature(const GlideConeSettings &s,
                  double clearance, double arrival) noexcept
{
  std::size_t h = std::hash<int>{}(int(s.mode));
  h = h * 31 + std::hash<double>{}(s.glide_ratio);
  h = h * 31 + std::hash<double>{}(s.max_altitude);
  h = h * 31 + std::hash<double>{}(s.cell_size);
  h = h * 31 + std::hash<unsigned>{}(s.iteration_cap);
  h = h * 31 + std::hash<double>{}(clearance);
  h = h * 31 + std::hash<double>{}(arrival);
  return h;
}

[[gnu::pure]]
static std::size_t
WaypointDisplaySignature(const WaypointRendererSettings &s) noexcept
{
  std::size_t h = std::hash<bool>{}(s.display_non_icao_airports);
  for (unsigned i = 0; i < unsigned(Waypoint::Type::COUNT); ++i)
    h = h * 31 + std::hash<bool>{}(s.display_types[i]);
  return h;
}

void
GlideConeJobController::Abort(GlideConeOverlay &overlay) noexcept
{
  const bool busy = awaiting_grid || awaiting_gpu || awaiting_contours ||
    gpu_worker.IsBusy() || contour_worker.IsBusy();
  if (busy)
    LogFmt("glidecones: AbortJobs gen={} awaiting_grid={} awaiting_gpu={} "
           "awaiting_contours={} gpu_busy={}",
           job_generation, awaiting_grid, awaiting_gpu, awaiting_contours,
           gpu_worker.IsBusy());
  gpu_worker.Cancel();
  contour_worker.Cancel();
  awaiting_grid = false;
  awaiting_gpu = false;
  awaiting_contours = false;
  overlay.ClearHoldRebase();
  if (busy) {
    ++job_generation;
    ++contour_generation;
  }
  (void)worker.TakeReady();
  (void)gpu_worker.TakeReady();
  (void)contour_worker.TakeReady();
}

void
GlideConeJobController::ClearFieldClaim(bool field_valid) noexcept
{
  /* A failed first build (DEM not ready, empty seeds, GPU context)
     used to leave computed_center/signature set so need_job never
     fired again until the aircraft moved. */
  if (!field_valid)
    computed_center = GeoPoint::Invalid();
  computed_signature = 0;
  last_job_attempt = std::chrono::steady_clock::now();
}

void
GlideConeJobController::InstallField(GlideConeField &field,
                                     GlideConeOverlay &overlay,
                                     GlideConePreparedGrid &&prepared,
                                     GlideConeResult &&result) noexcept
{
  LogFmt("glidecones: InstallField gen={} {}x{} seeds={} cell={:.0f}x{:.0f}m",
         prepared.generation, result.width, result.height,
         prepared.grid.seeds.size(),
         prepared.grid.cell_size_x_m, prepared.grid.cell_size_y_m);
  field.result = std::move(result);
  field.bounds = prepared.bounds;
  field.cell_size_m = std::sqrt(prepared.grid.cell_size_x_m *
                                prepared.grid.cell_size_y_m);
  field.cell_size_x_m = prepared.grid.cell_size_x_m;
  field.cell_size_y_m = prepared.grid.cell_size_y_m;
  field.glide_ratio = prepared.grid.glide_ratio;
  field.max_alt = prepared.grid.max_alt;
  field.home_x = prepared.grid.seeds.front().x;
  field.home_y = prepared.grid.seeds.front().y;
  field.seeds = std::move(prepared.grid.seeds);
  field.elevation = std::move(prepared.grid.elevation);
  field.contour_lines.clear();
  computed_contours = false;
  awaiting_contours = false;
  ++contour_generation;
  contour_worker.Cancel();
  (void)contour_worker.TakeReady();

  const bool drop = drop_labels_on_install;
  drop_labels_on_install = true;
  overlay.OnFieldInstalled(drop);
}

void
GlideConeJobController::RequestContours(GlideConeField &field,
                                        bool polylines) noexcept
{
  if (!field.IsValid())
    return;

  ++contour_generation;
  GlideConeField snapshot = field;
  snapshot.contour_lines.clear();
  if (contour_worker.Request(contour_generation, std::move(snapshot),
                             polylines)) {
    awaiting_contours = true;
    computed_contours = false;
  } else {
    awaiting_contours = false;
    LogFmt("glidecones: contour.Request failed gen={}", contour_generation);
  }
}

void
GlideConeJobController::ClearContours(GlideConeField &field,
                                      GlideConeOverlay &overlay) noexcept
{
  contour_worker.Cancel();
  (void)contour_worker.TakeReady();
  awaiting_contours = false;
  field.contour_lines.clear();
  computed_contours = false;
  overlay.InvalidateLabels();
}

void
GlideConeJobController::UpdateContours(GlideConeField &field,
                                       GlideConeOverlay &overlay,
                                       const GlideConeSettings &gc) noexcept
{
  if (auto ready = contour_worker.TakeReady()) {
    awaiting_contours = false;
    if (ready->generation == contour_generation) {
      field.contour_lines = std::move(ready->contour_lines);
      computed_contours = true;
      computed_contour_polylines = ready->polylines;
      overlay.OnContoursReady();
      LogFmt("glidecones: contours ready gen={} lines={} polylines={}",
             ready->generation, field.contour_lines.size(),
             ready->polylines);
    }
  } else if (!contour_worker.IsBusy()) {
    awaiting_contours = false;
  }

  const bool need_contours = !computed_contours ||
    computed_contour_polylines != gc.contour_polylines;
  if (need_contours && !awaiting_contours && !contour_worker.IsBusy())
    RequestContours(field, gc.contour_polylines);
}

bool
GlideConeJobController::Update(GlideConeField &field,
                               GlideConeOverlay &overlay,
                               GeoPoint aircraft, bool aircraft_valid,
                               GeoPoint target, bool target_valid,
                               GeoPoint pending_seed, bool pending_valid,
                               const ComputerSettings &settings,
                               const RasterTerrain *terrain,
                               const Waypoints *waypoints,
                               const WaypointRendererSettings &waypoint_settings) noexcept
{
  const GlideConeSettings &gc = settings.glide_cone;
  const auto mode = gc.mode;
  const double clearance =
    settings.task.route_planner.safety_height_terrain;
  const double arrival = settings.task.safety_height_arrival;

  std::size_t signature = SettingsSignature(gc, clearance, arrival);
  if (mode == GlideConeSettings::Mode::COMBINED)
    signature = signature * 31 + WaypointDisplaySignature(waypoint_settings);

  if (mode == GlideConeSettings::Mode::OFF || terrain == nullptr ||
      !GlideConeGpuSession::Available()) {
    if (field.IsValid() || awaiting_grid || awaiting_gpu ||
        gpu_worker.IsBusy())
      LogFmt("glidecones: idle mode={} terrain={} gpu_avail={} "
             "(clearing pipeline)",
             ModeName(mode), terrain != nullptr,
             GlideConeGpuSession::Available());
    Abort(overlay);
    field.Clear();
    computed_center = GeoPoint::Invalid();
    return false;
  }

  GeoPoint center = GeoPoint::Invalid();
  const double radius_m = gc.WindowRadiusM();
  double recompute_threshold_m = GLIDE_CONE_SEED_EPSILON_M;
  std::vector<GeoPoint> single_seeds;
  bool have_center = false;
  const char *seed_source = "-";

  if (mode == GlideConeSettings::Mode::SINGLE) {
    GeoPoint seed;
    bool valid;
    if (target_valid) {
      seed = target;
      valid = true;
      seed_source = "map_target";
    } else {
      seed = pending_seed;
      valid = pending_valid;
      seed_source = "pending";
    }

    if (!valid) {
      if (field.IsValid() || awaiting_grid || awaiting_gpu)
        LogFmt("glidecones: single: no seed (target_valid={} pending={})",
               target_valid, pending_valid);
      Abort(overlay);
      field.Clear();
      computed_center = GeoPoint::Invalid();
      return false;
    }

    center = seed;
    single_seeds.push_back(seed);
    have_center = true;
    recompute_threshold_m = GLIDE_CONE_SEED_EPSILON_M;
  } else if (aircraft_valid && waypoints != nullptr) {
    center = aircraft;
    have_center = true;
    seed_source = "aircraft";
    recompute_threshold_m = GLIDE_CONE_MAX_OFFSET_FROM_CENTER * radius_m;
  } else if (mode == GlideConeSettings::Mode::COMBINED) {
    if (field.IsValid() || awaiting_grid || awaiting_gpu)
      LogFmt("glidecones: combined: waiting aircraft={} waypoints={}",
             aircraft_valid, waypoints != nullptr);
  }

  const auto now = std::chrono::steady_clock::now();
  const bool sig_changed = signature != computed_signature;
  if (sig_changed && signature != debounce_signature) {
    debounce_signature = signature;
    debounce_since = now;
  }
  const bool sig_ready = sig_changed &&
    now - debounce_since >= GLIDE_CONE_DEBOUNCE;

  bool waypoints_ready = false;
  Serial waypoint_serial{};
  if (mode == GlideConeSettings::Mode::COMBINED && waypoints != nullptr) {
    waypoint_serial = waypoints->GetSerial();
    if (waypoint_serial != computed_waypoint_serial &&
        waypoint_serial != debounce_waypoint_serial) {
      debounce_waypoint_serial = waypoint_serial;
      waypoint_debounce_since = now;
    }
    waypoints_ready =
      waypoint_serial != computed_waypoint_serial &&
      now - waypoint_debounce_since >= GLIDE_CONE_DEBOUNCE;
  }

  const bool center_moved = have_center &&
    (!computed_center.IsValid() ||
     computed_center.DistanceS(center) > recompute_threshold_m);

  if (center_moved && !overlay.IsHoldRebase()) {
    overlay.HoldRebase();
    LogFmt("glidecones: labels hold rebase (center moved "
           "by {:.0f}m, threshold {:.0f}m)",
           computed_center.IsValid()
             ? computed_center.DistanceS(center) : -1.0,
           recompute_threshold_m);
  }

  const bool cooled_down =
    now - last_job_attempt >= GLIDE_CONE_DEBOUNCE;
  const bool need_job = have_center && !IsBusy() && cooled_down &&
    (!field.IsValid() || center_moved || sig_ready || waypoints_ready);

  if (need_job) {
    const GeoPoint job_center =
      (!center_moved && computed_center.IsValid()) ? computed_center
                                                   : center;

    ++job_generation;
    drop_labels_on_install = center_moved || !computed_center.IsValid();
    gpu_worker.Cancel();
    awaiting_gpu = false;
    (void)gpu_worker.TakeReady();
    last_job_attempt = now;

    LogFmt("glidecones: {} request gen={} via={} lat={:.5f} lon={:.5f} "
           "radius={:.0f}m L/D={:.0f} max_alt={:.0f} cell={:.0f} "
           "why: center={} sig={} wpts={} empty={} seeds={}",
           ModeName(mode), job_generation, seed_source,
           job_center.latitude.Degrees(), job_center.longitude.Degrees(),
           radius_m, gc.glide_ratio, gc.max_altitude, gc.cell_size,
           center_moved, sig_ready, waypoints_ready,
           !field.IsValid(), single_seeds.size());

    GlideConeGridRequest request;
    request.generation = job_generation;
    request.center = job_center;
    request.radius_m = radius_m;
    request.glide_ratio = gc.glide_ratio;
    request.max_altitude = gc.max_altitude;
    request.cell_size = gc.cell_size;
    request.iteration_cap = gc.iteration_cap;
    request.clearance = clearance;
    request.arrival = arrival;
    request.combined = mode == GlideConeSettings::Mode::COMBINED;
    request.seeds = std::move(single_seeds);
    request.waypoint_settings = waypoint_settings;
    if (worker.Request(std::move(request), terrain, waypoints)) {
      awaiting_grid = true;
      computed_center = job_center;
      computed_signature = signature;
      if (mode == GlideConeSettings::Mode::COMBINED)
        computed_waypoint_serial = waypoint_serial;
    } else {
      awaiting_grid = false;
      ClearFieldClaim(field.IsValid());
      LogFmt("glidecones: worker.Request failed gen={}", job_generation);
    }
  }

  if (auto prepared = worker.TakeReady()) {
    if (prepared->generation == job_generation) {
      awaiting_grid = false;
      if (prepared->grid.IsValid()) {
        LogFmt("glidecones: grid ready gen={} {}x{} seeds={} "
               "cell={:.0f}x{:.0f}m → GPU worker",
               prepared->generation, prepared->grid.width,
               prepared->grid.height, prepared->grid.seeds.size(),
               prepared->grid.cell_size_x_m, prepared->grid.cell_size_y_m);
        if (gpu_worker.Request(std::move(prepared)))
          awaiting_gpu = true;
        else {
          ClearFieldClaim(field.IsValid());
          LogFmt("glidecones: GPU worker.Request failed gen={}",
                 job_generation);
        }
      } else {
        ClearFieldClaim(field.IsValid());
        LogFmt("glidecones: grid ready but invalid gen={} "
               "(build failed or empty seeds)",
               prepared->generation);
      }
    } else {
      LogFmt("glidecones: drop stale grid gen={} (current={})",
             prepared->generation, job_generation);
    }
  }

  if (auto gpu_ready = gpu_worker.TakeReady()) {
    awaiting_gpu = false;
    if (gpu_ready->prepared != nullptr &&
        gpu_ready->prepared->generation == job_generation &&
        gpu_ready->ok && gpu_ready->result.IsValid()) {
      LogFmt("glidecones: GPU Finish ok gen={}",
             gpu_ready->prepared->generation);
      if (gpu_ready->hit_iteration_cap)
        Message::AddMessage(
          _("GlideCone compute stopped, raise iteration cap"));
      InstallField(field, overlay,
                   std::move(*gpu_ready->prepared),
                   std::move(gpu_ready->result));
    } else {
      ClearFieldClaim(field.IsValid());
      LogFmt("glidecones: GPU Finish failed/stale gen={}", job_generation);
    }
  } else if (!gpu_worker.IsBusy()) {
    awaiting_gpu = false;
  }

  return true;
}
