// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include <string>

/**
 * A small in-memory, thread-safe log buffer for the glide cone pipeline.
 *
 * It lets the feature be diagnosed on a device without adb: messages are
 * appended from the UI and draw threads, and can be copied to the system
 * clipboard via the "Copy glide cone log" input event.
 */
namespace GlideConeLog {

void Clear() noexcept;

[[gnu::format(printf, 1, 2)]]
void Add(const char *fmt, ...) noexcept;

/** Return a snapshot of the current log buffer. */
[[gnu::pure]]
std::string Copy() noexcept;

} // namespace GlideConeLog
