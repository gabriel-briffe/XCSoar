// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeLog.hpp"
#include "thread/Mutex.hxx"

#include <cstdarg>
#include <cstdio>

namespace {
Mutex mutex;
std::string buffer;

/** Keep only the most recent bytes so the buffer cannot grow unbounded. */
constexpr std::size_t MAX_BYTES = 32768;
}

void
GlideConeLog::Clear() noexcept
{
  const std::lock_guard lock{mutex};
  buffer.clear();
}

void
GlideConeLog::Add(const char *fmt, ...) noexcept
{
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;

  const std::lock_guard lock{mutex};
  buffer.append(line);
  buffer.push_back('\n');
  if (buffer.size() > MAX_BYTES)
    buffer.erase(0, buffer.size() - MAX_BYTES);
}

std::string
GlideConeLog::Copy() noexcept
{
  const std::lock_guard lock{mutex};
  return buffer;
}
