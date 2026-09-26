// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

struct SoundSettings;

/**
 * Apply #SoundSettings to runtime audio (WAV resources + audio vario).
 * Call after load and whenever the settings change.
 */
void
ApplySoundSettings(const SoundSettings &settings) noexcept;

[[gnu::pure]]
bool
IsSoundEnabled() noexcept;

bool
PlayResource(const char *resource_name);
