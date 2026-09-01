// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

struct PageSettingDescriptor;

namespace PageSettingProfile {

[[nodiscard]]
int
Load(const PageSettingDescriptor &desc) noexcept;

void
Save(const PageSettingDescriptor &desc, int value) noexcept;

} // namespace PageSettingProfile
