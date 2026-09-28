// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Profile/Profile.hpp"

#include <functional>
#include <string_view>
#include <type_traits>

namespace ConfigPanel {
void BorrowExtraButton(unsigned i, const char *caption,
                       std::function<void()> callback) noexcept;
void ReturnExtraButton(unsigned i);

/**
 * Copy #value into #dest when it differs, and set #changed.
 * @return true when #dest was updated
 */
template<typename T>
bool
CommitSetting(bool &changed, T &dest, T value) noexcept
{
  if (dest == value)
    return false;

  dest = value;
  changed = true;
  return true;
}

/**
 * As above, and write the profile key.  An enumeration is stored
 * as an unsigned value, which is how the row form used to save it.
 */
template<typename T>
bool
CommitSetting(bool &changed, T &dest, T value,
              std::string_view key) noexcept
{
  if (!CommitSetting(changed, dest, value))
    return false;

  if constexpr (std::is_enum_v<T>)
    Profile::Set(key, static_cast<unsigned>(value));
  else
    Profile::Set(key, value);
  return true;
}
};
