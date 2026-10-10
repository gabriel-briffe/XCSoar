// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeGpuWorker.hpp"
#include "GlideConeCompute.hpp"
#include "GlideConeDownward.hpp"
#include "thread/StandbyThread.hpp"
#include "thread/Mutex.hxx"

#ifdef HAVE_GLES_COMPUTE
#include "ui/egl/System.hpp"
#include "ui/opengl/GLESCompute.hpp"
#include "GlideConeTiming.hpp"
#include "LogFile.hpp"
#endif

#include <atomic>
#include <chrono>
#include <functional>
#include <utility>

struct GlideConeGpuWorker::Impl
#ifdef HAVE_GLES_COMPUTE
  final : private StandbyThread
#endif
{
#ifdef HAVE_GLES_COMPUTE
  Impl() noexcept
    :StandbyThread("GlideConeGPU") {}

  ~Impl() noexcept {
    LockStop();
    DestroySharedContext();
  }

  bool Request(std::unique_ptr<GlideConePreparedGrid> prepared) noexcept {
    try {
      const std::lock_guard lock{mutex};
      if (!EnsureSharedContext())
        return false;

      if (running_gen != 0)
        cancel_gen.store(running_gen, std::memory_order_relaxed);

      next = std::move(prepared);
      GLIDECONE_TIMING_ONLY(cone_request_us = GlideConeTiming::NowUs();)
      Trigger();
      return true;
    } catch (...) {
      return false;
    }
  }

  std::unique_ptr<GlideConeGpuReady> TakeReady() noexcept {
    const std::lock_guard lock{mutex};
    return std::move(ready);
  }

  bool RequestDownward(std::unique_ptr<GlideConeDownwardJob> job) noexcept {
    if (job == nullptr || job->width == 0)
      return false;
    try {
      const std::lock_guard lock{mutex};
      if (!EnsureSharedContext())
        return false;
      if (down_running_gen != 0)
        down_cancel.store(down_running_gen, std::memory_order_relaxed);
      down_next = std::move(job);
      GLIDECONE_TIMING_ONLY(down_request_us = GlideConeTiming::NowUs();)
      Trigger();
      return true;
    } catch (...) {
      return false;
    }
  }

  std::unique_ptr<GlideConeDownwardReady> TakeDownward() noexcept {
    const std::lock_guard lock{mutex};
    return std::move(down_ready);
  }

  void CancelDownward() noexcept {
    const std::lock_guard lock{mutex};
    std::uint64_t gen = down_running_gen;
    if (gen == 0 && down_next != nullptr)
      gen = down_next->generation;
    if (gen != 0)
      down_cancel.store(gen, std::memory_order_relaxed);
    down_next.reset();
  }

  void Cancel() noexcept {
    const std::lock_guard lock{mutex};
    std::uint64_t gen = running_gen;
    if (gen == 0 && next != nullptr)
      gen = next->generation;
    if (gen != 0)
      cancel_gen.store(gen, std::memory_order_relaxed);
    next.reset();
  }

  bool IsBusy() noexcept {
    const std::lock_guard lock{mutex};
    return StandbyThread::IsBusy() || next != nullptr || ready != nullptr;
  }

  void SetReadyCallback(std::function<void()> callback) noexcept {
    const std::lock_guard lock{mutex};
    ready_callback = std::move(callback);
  }

private:
  void NotifyReady() noexcept {
    if (ready_callback)
      ready_callback();
  }

  bool EnsureSharedContext() noexcept {
    if (compute_ctx != EGL_NO_CONTEXT)
      return true;

    dpy = eglGetCurrentDisplay();
    const EGLContext ui_ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || ui_ctx == EGL_NO_CONTEXT) {
      return false;
    }

    EGLint config_id = 0;
    if (!eglQueryContext(dpy, ui_ctx, EGL_CONFIG_ID, &config_id)) {
      return false;
    }

    const EGLint cfg_attrs[] = { EGL_CONFIG_ID, config_id, EGL_NONE };
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfg_attrs, &config, 1, &n) || n < 1) {
      return false;
    }

    static constexpr EGLint es31_attrs[] = {
      EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 1,
      EGL_NONE
    };

    compute_ctx = eglCreateContext(dpy, config, ui_ctx, es31_attrs);
    if (compute_ctx == EGL_NO_CONTEXT) {
      /* UI probe said ES 3.1 was available, but the shared compute
         context failed — never retry. */
      SetGLES31ComputeAvailable(false);
      return false;
    }

    static constexpr EGLint pbuffer_attrs[] = {
      EGL_WIDTH, 1,
      EGL_HEIGHT, 1,
      EGL_NONE
    };
    compute_surf = eglCreatePbufferSurface(dpy, config, pbuffer_attrs);
    if (compute_surf == EGL_NO_SURFACE) {
      eglDestroyContext(dpy, compute_ctx);
      compute_ctx = EGL_NO_CONTEXT;
      return false;
    }

    return true;
  }

  void DestroySharedContext() noexcept {
    if (compute_ctx == EGL_NO_CONTEXT)
      return;

    if (eglMakeCurrent(dpy, compute_surf, compute_surf, compute_ctx)) {
      session.DestroyGL();
      DestroyGlideConeDownward();
      eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (compute_surf != EGL_NO_SURFACE)
      eglDestroySurface(dpy, compute_surf);
    eglDestroyContext(dpy, compute_ctx);
    compute_surf = EGL_NO_SURFACE;
    compute_ctx = EGL_NO_CONTEXT;
    dpy = EGL_NO_DISPLAY;
  }

  void Tick() noexcept override {
    /* Prefer throughput over idle niceness — UI has its own GL context. */
    std::unique_ptr<GlideConePreparedGrid> job = std::move(next);
    std::unique_ptr<GlideConeDownwardJob> down = std::move(down_next);
    if (job == nullptr && down == nullptr)
      return;
#if GLIDECONE_TIMING
    /* queue wait = Request*() (last, superseding one) -> Tick start */
    namespace GT = GlideConeTiming;
    const std::uint64_t t_tick = GT::NowUs();
    const bool timed_cone = job != nullptr;
    const bool timed_down = down != nullptr;
    const std::uint64_t cone_queue_us =
      timed_cone && cone_request_us != 0 ? t_tick - cone_request_us : 0;
    const std::uint64_t down_queue_us =
      timed_down && down_request_us != 0 ? t_tick - down_request_us : 0;
    std::uint64_t make_current_us = 0, cone_run_us = 0, down_run_us = 0;
#endif

    const std::uint64_t gen = job != nullptr ? job->generation : 0;
    const std::uint64_t down_gen = down != nullptr ? down->generation : 0;
    running_gen = gen;
    down_running_gen = down_gen;
    cancel_gen.store(0, std::memory_order_relaxed);
    if (down_gen != 0)
      down_cancel.store(0, std::memory_order_relaxed);

    const auto should_abort = [&]() noexcept {
      if (IsStopped())
        return true;
      return cancel_gen.load(std::memory_order_relaxed) == gen && gen != 0;
    };
    const auto down_should_abort = [&]() noexcept {
      if (IsStopped())
        return true;
      return down_cancel.load(std::memory_order_relaxed) == down_gen &&
        down_gen != 0;
    };

    auto out = std::make_unique<GlideConeGpuReady>();
    std::unique_ptr<GlideConeDownwardReady> down_out;
    if (job != nullptr)
      out->prepared = std::move(job);

    bool ok = false;
    bool hit_cap = false;
    bool down_ok = false;
    {
      const ScopeUnlock unlock{mutex};

      GLIDECONE_TIMING_ONLY(std::uint64_t tp = GT::NowUs();)
      unsigned gpu_run_ms = 0;
      if (eglMakeCurrent(dpy, compute_surf, compute_surf, compute_ctx)) {
        GLIDECONE_TIMING_ONLY(make_current_us = GT::SinceUs(tp);
                              tp = GT::NowUs();)
        if (out->prepared != nullptr) {
          using Clock = std::chrono::steady_clock;
          const auto t_run = Clock::now();
          ok = session.Run(out->prepared->grid, should_abort, out->result,
                           &hit_cap);
          gpu_run_ms = unsigned(std::chrono::duration_cast<
            std::chrono::milliseconds>(Clock::now() - t_run).count());
        }
        GLIDECONE_TIMING_ONLY(cone_run_us = GT::SinceUs(tp);
                              tp = GT::NowUs();)
        if (down != nullptr) {
          down_out = std::make_unique<GlideConeDownwardReady>();
          down_ok = RunGlideConeDownward(*down, down_should_abort, *down_out);
          down_out->ok = down_ok;
        }
        GLIDECONE_TIMING_ONLY(down_run_us = GT::SinceUs(tp);)
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      }
      if (out->prepared != nullptr)
        out->compute_ms = out->prepared->compute_ms + gpu_run_ms;
#if GLIDECONE_TIMING
      /* logged outside the mutex; the cone job runs before the
         optional-area job in the same Tick, so the latter's latency
         includes cone_run */
      if (timed_cone)
        LogFmt("GlideCone cone: gpu worker queue={:.3f}ms "
               "make_current={:.3f}ms run={:.3f}ms ok={} "
               "request_to_ready={:.3f}ms",
               GT::Ms(cone_queue_us), GT::Ms(make_current_us),
               GT::Ms(cone_run_us), ok,
               GT::Ms(cone_queue_us + make_current_us + cone_run_us));
      if (timed_down)
        LogFmt("GlideCone options: gpu worker queue={:.3f}ms "
               "make_current={:.3f}ms cone_before={:.3f}ms run={:.3f}ms "
               "ok={} request_to_ready={:.3f}ms",
               GT::Ms(down_queue_us), GT::Ms(make_current_us),
               GT::Ms(cone_run_us), GT::Ms(down_run_us), down_ok,
               GT::Ms(GT::SinceUs(t_tick) + down_queue_us));
#endif
    }

    if (out->prepared != nullptr) {
      out->ok = ok;
      out->hit_iteration_cap = hit_cap;
      if (ok)
        ready = std::move(out);
    }
    running_gen = 0;
    down_running_gen = 0;
    if (down != nullptr && down_out == nullptr) {
      down_out = std::make_unique<GlideConeDownwardReady>();
      down_out->generation = down_gen;
      down_out->ok = false;
    }
    if (down_out != nullptr)
      down_ready = std::move(down_out);

    NotifyReady();
  }

  using StandbyThread::mutex;

  std::unique_ptr<GlideConePreparedGrid> next;
  std::unique_ptr<GlideConeGpuReady> ready;
  std::unique_ptr<GlideConeDownwardJob> down_next;
  std::unique_ptr<GlideConeDownwardReady> down_ready;
  std::uint64_t running_gen = 0;
  std::uint64_t down_running_gen = 0;
  std::atomic<std::uint64_t> cancel_gen{0};
  std::atomic<std::uint64_t> down_cancel{0};
  std::function<void()> ready_callback;
#if GLIDECONE_TIMING
  /** GlideConeTiming::NowUs() of the last Request()/RequestDownward()
      (protected by mutex). */
  std::uint64_t cone_request_us = 0;
  std::uint64_t down_request_us = 0;
#endif

  EGLDisplay dpy = EGL_NO_DISPLAY;
  EGLConfig config{};
  EGLContext compute_ctx = EGL_NO_CONTEXT;
  EGLSurface compute_surf = EGL_NO_SURFACE;

  GlideConeGpuSession session;
#else
  bool Request(std::unique_ptr<GlideConePreparedGrid>) noexcept {
    return false;
  }
  std::unique_ptr<GlideConeGpuReady> TakeReady() noexcept {
    return nullptr;
  }
  bool RequestDownward(std::unique_ptr<GlideConeDownwardJob>) noexcept {
    return false;
  }
  std::unique_ptr<GlideConeDownwardReady> TakeDownward() noexcept {
    return nullptr;
  }
  void CancelDownward() noexcept {}
  void Cancel() noexcept {}
  void SetReadyCallback(std::function<void()>) noexcept {}
  bool IsBusy() noexcept {
    return false;
  }
#endif
};

GlideConeGpuWorker::GlideConeGpuWorker() noexcept
  :impl(new Impl) {}

GlideConeGpuWorker::~GlideConeGpuWorker() noexcept
{
  delete impl;
}

bool
GlideConeGpuWorker::Request(std::unique_ptr<GlideConePreparedGrid> prepared) noexcept
{
  return impl->Request(std::move(prepared));
}

std::unique_ptr<GlideConeGpuReady>
GlideConeGpuWorker::TakeReady() noexcept
{
  return impl->TakeReady();
}

bool
GlideConeGpuWorker::RequestDownward(std::unique_ptr<GlideConeDownwardJob> job) noexcept
{
  return impl->RequestDownward(std::move(job));
}

std::unique_ptr<GlideConeDownwardReady>
GlideConeGpuWorker::TakeDownward() noexcept
{
  return impl->TakeDownward();
}

void
GlideConeGpuWorker::CancelDownward() noexcept
{
  impl->CancelDownward();
}

void
GlideConeGpuWorker::Cancel() noexcept
{
  impl->Cancel();
}

void
GlideConeGpuWorker::SetReadyCallback(std::function<void()> callback) noexcept
{
  impl->SetReadyCallback(std::move(callback));
}

bool
GlideConeGpuWorker::IsBusy() const noexcept
{
  return const_cast<Impl *>(impl)->IsBusy();
}
