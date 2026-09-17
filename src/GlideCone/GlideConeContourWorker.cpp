// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeContourWorker.hpp"

#include <utility>

bool
GlideConeContourWorker::Request(std::uint64_t generation,
                                GlideConeField field,
                                double interval_m) noexcept
{
  try {
    const std::lock_guard lock{mutex};
    next_generation = generation;
    next_interval_m = interval_m;
    next_field = std::make_unique<GlideConeField>(std::move(field));
    next_field->contour_lines.clear();
    Trigger();
    return true;
  } catch (...) {
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
  double interval_m;
  {
    job = std::move(next_field);
    if (job == nullptr)
      return;
    gen = next_generation;
    interval_m = next_interval_m;
  }

  auto out = std::make_unique<GlideConeContourReady>();
  out->generation = gen;

  {
    const ScopeUnlock unlock{mutex};
    job->BuildContours(interval_m);
    out->contour_lines = std::move(job->contour_lines);
  }

  if (gen != next_generation) {
    NotifyReady();
    return;
  }

  ready = std::move(out);
  NotifyReady();
}
