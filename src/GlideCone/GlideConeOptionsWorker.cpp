// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeOptionsWorker.hpp"
#include "GlideConeOptions.hpp"

#include <utility>

bool
GlideConeOptionsWorker::Request(
  std::unique_ptr<GlideConeOptionsCpuJob> job) noexcept
{
  try {
    const std::lock_guard lock{mutex};
    next = std::move(job);
    Trigger();
    return true;
  } catch (...) {
    return false;
  }
}

std::unique_ptr<GlideConeOptionsCpuReady>
GlideConeOptionsWorker::TakeReady() noexcept
{
  const std::lock_guard lock{mutex};
  return std::move(ready);
}

void
GlideConeOptionsWorker::Cancel() noexcept
{
  const std::lock_guard lock{mutex};
  next.reset();
  ready.reset();
}

bool
GlideConeOptionsWorker::IsBusy() noexcept
{
  const std::lock_guard lock{mutex};
  return StandbyThread::IsBusy() || next != nullptr || ready != nullptr;
}

void
GlideConeOptionsWorker::NotifyReady() noexcept
{
  if (ready_callback)
    ready_callback();
}

void
GlideConeOptionsWorker::Tick() noexcept
{
  SetIdlePriority();

  std::unique_ptr<GlideConeOptionsCpuJob> job;
  {
    job = std::move(next);
    if (job == nullptr)
      return;
  }

  const std::uint64_t gen = job->generation;
  auto out = std::make_unique<GlideConeOptionsCpuReady>();
  out->generation = gen;

  {
    const ScopeUnlock unlock{mutex};
    GlideConeOptions::ComputeCpu(*job, *out);
  }

  if (next != nullptr && next->generation != 0 &&
      next->generation != gen) {
    NotifyReady();
    return;
  }

  ready = std::move(out);
  NotifyReady();
}
