// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeContourWorker.hpp"
#include "LogFile.hpp"

#include <utility>

bool
GlideConeContourWorker::Request(std::uint64_t generation,
                                GlideConeField field,
                                bool polylines,
                                double interval_m) noexcept
{
  try {
    const std::lock_guard lock{mutex};
    next_generation = generation;
    next_polylines = polylines;
    next_interval_m = interval_m;
    next_field = std::make_unique<GlideConeField>(std::move(field));
    next_field->contour_lines.clear();
    LogFmt("glidecones: contour.Request gen={} polylines={} {}x{}",
           generation, polylines,
           next_field->result.width, next_field->result.height);
    Trigger();
    return true;
  } catch (...) {
    LogFmt("glidecones: contour.Request exception");
    return false;
  }
}

std::unique_ptr<GlideConeContourReady>
GlideConeContourWorker::TakeReady() noexcept
{
  const std::lock_guard lock{mutex};
  return std::move(ready);
}

void
GlideConeContourWorker::Cancel() noexcept
{
  const std::lock_guard lock{mutex};
  next_field.reset();
  ready.reset();
}

bool
GlideConeContourWorker::IsBusy() noexcept
{
  const std::lock_guard lock{mutex};
  return StandbyThread::IsBusy() || next_field != nullptr ||
    ready != nullptr;
}

void
GlideConeContourWorker::NotifyReady() noexcept
{
  if (ready_callback)
    ready_callback();
}

void
GlideConeContourWorker::Tick() noexcept
{
  SetIdlePriority();

  std::unique_ptr<GlideConeField> job;
  std::uint64_t gen;
  bool polylines;
  double interval_m;
  {
    job = std::move(next_field);
    if (job == nullptr)
      return;
    gen = next_generation;
    polylines = next_polylines;
    interval_m = next_interval_m;
  }

  LogFmt("glidecones: contour.Tick start gen={} polylines={}",
         gen, polylines);

  auto out = std::make_unique<GlideConeContourReady>();
  out->generation = gen;
  out->polylines = polylines;

  {
    const ScopeUnlock unlock{mutex};
    job->BuildContours(interval_m, polylines);
    out->contour_lines = std::move(job->contour_lines);
  }

  if (gen != next_generation) {
    LogFmt("glidecones: contour.Tick drop gen={} (superseded by {})",
           gen, next_generation);
    NotifyReady();
    return;
  }

  LogFmt("glidecones: contour.Tick done gen={} lines={}",
         gen, out->contour_lines.size());
  ready = std::move(out);
  NotifyReady();
}
