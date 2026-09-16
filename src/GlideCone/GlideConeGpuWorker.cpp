// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeGpuWorker.hpp"
#include "GlideConeCompute.hpp"
#include "LogFile.hpp"
#include "thread/StandbyThread.hpp"
#include "thread/Mutex.hxx"

#ifdef HAVE_GLES_COMPUTE
#include "ui/egl/System.hpp"
#endif

#include <atomic>
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
      LogFmt("glidecones: GPU worker.Request gen={}", next->generation);
      Trigger();
      return true;
    } catch (...) {
      LogFmt("glidecones: GPU worker.Request exception");
      return false;
    }
  }

  std::unique_ptr<GlideConeGpuReady> TakeReady() noexcept {
    const std::lock_guard lock{mutex};
    return std::move(ready);
  }

  void Cancel() noexcept {
    const std::lock_guard lock{mutex};
    std::uint64_t gen = running_gen;
    if (gen == 0 && next != nullptr)
      gen = next->generation;
    if (gen != 0)
      cancel_gen.store(gen, std::memory_order_relaxed);
    next.reset();
    LogFmt("glidecones: GPU worker.Cancel gen={}", gen);
  }

  bool IsBusy() noexcept {
    const std::lock_guard lock{mutex};
    return StandbyThread::IsBusy() || next != nullptr || ready != nullptr;
  }

private:
  bool EnsureSharedContext() noexcept {
    if (compute_ctx != EGL_NO_CONTEXT)
      return true;

    dpy = eglGetCurrentDisplay();
    const EGLContext ui_ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || ui_ctx == EGL_NO_CONTEXT) {
      LogFmt("glidecones: GPU worker: no current EGL display/context");
      return false;
    }

    EGLint config_id = 0;
    if (!eglQueryContext(dpy, ui_ctx, EGL_CONFIG_ID, &config_id)) {
      LogFmt("glidecones: GPU worker: eglQueryContext failed {:#x}",
             eglGetError());
      return false;
    }

    const EGLint cfg_attrs[] = { EGL_CONFIG_ID, config_id, EGL_NONE };
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfg_attrs, &config, 1, &n) || n < 1) {
      LogFmt("glidecones: GPU worker: eglChooseConfig failed {:#x}",
             eglGetError());
      return false;
    }

    static constexpr EGLint es31_attrs[] = {
      EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 1,
      EGL_NONE
    };

    compute_ctx = eglCreateContext(dpy, config, ui_ctx, es31_attrs);
    if (compute_ctx == EGL_NO_CONTEXT) {
      LogFmt("glidecones: GPU worker: eglCreateContext(share) failed {:#x}",
             eglGetError());
      return false;
    }

    static constexpr EGLint pbuffer_attrs[] = {
      EGL_WIDTH, 1,
      EGL_HEIGHT, 1,
      EGL_NONE
    };
    compute_surf = eglCreatePbufferSurface(dpy, config, pbuffer_attrs);
    if (compute_surf == EGL_NO_SURFACE) {
      LogFmt("glidecones: GPU worker: pbuffer failed {:#x}", eglGetError());
      eglDestroyContext(dpy, compute_ctx);
      compute_ctx = EGL_NO_CONTEXT;
      return false;
    }

    LogFmt("glidecones: GPU worker: shared ES 3.1 context ready");
    return true;
  }

  void DestroySharedContext() noexcept {
    if (compute_ctx == EGL_NO_CONTEXT)
      return;

    if (eglMakeCurrent(dpy, compute_surf, compute_surf, compute_ctx)) {
      session.DestroyGL();
      eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (compute_surf != EGL_NO_SURFACE)
      eglDestroySurface(dpy, compute_surf);
    eglDestroyContext(dpy, compute_ctx);
    compute_surf = EGL_NO_SURFACE;
    compute_ctx = EGL_NO_CONTEXT;
    dpy = EGL_NO_DISPLAY;
    LogFmt("glidecones: GPU worker: shared context destroyed");
  }

  void Tick() noexcept override {
    /* Prefer throughput over idle niceness — UI has its own GL context. */
    std::unique_ptr<GlideConePreparedGrid> job = std::move(next);
    if (job == nullptr)
      return;

    const std::uint64_t gen = job->generation;
    running_gen = gen;
    cancel_gen.store(0, std::memory_order_relaxed);

    LogFmt("glidecones: GPU worker.Tick start gen={} {}x{}",
           gen, job->grid.width, job->grid.height);

    const auto should_abort = [&]() noexcept {
      if (IsStopped())
        return true;
      return cancel_gen.load(std::memory_order_relaxed) == gen;
    };

    auto out = std::make_unique<GlideConeGpuReady>();
    out->prepared = std::move(job);

    bool ok = false;
    {
      const ScopeUnlock unlock{mutex};

      if (!eglMakeCurrent(dpy, compute_surf, compute_surf, compute_ctx)) {
        LogFmt("glidecones: GPU worker: MakeCurrent failed {:#x}",
               eglGetError());
      } else {
        ok = session.Run(out->prepared->grid, should_abort, out->result);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      }
    }

    out->ok = ok;
    running_gen = 0;
    LogFmt("glidecones: GPU worker.Tick done gen={} ok={}", gen, ok);

    if (!ok)
      return;

    ready = std::move(out);
  }

  using StandbyThread::mutex;

  std::unique_ptr<GlideConePreparedGrid> next;
  std::unique_ptr<GlideConeGpuReady> ready;
  std::uint64_t running_gen = 0;
  std::atomic<std::uint64_t> cancel_gen{0};

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
  void Cancel() noexcept {}
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

void
GlideConeGpuWorker::Cancel() noexcept
{
  impl->Cancel();
}

bool
GlideConeGpuWorker::IsBusy() const noexcept
{
  return const_cast<Impl *>(impl)->IsBusy();
}
