// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Macros to define bundle field accessors and accessor tables for
 * #PageSettingModuleImpl.
 */

#define PAGE_SETTING_FIELD_BOOL(Suffix, member) \
  [[nodiscard]] static int \
  Get##Suffix(const Bundle &bundle) noexcept \
  { \
    return bundle.member ? 1 : 0; \
  } \
  static void \
  Set##Suffix(Bundle &bundle, int value) noexcept \
  { \
    bundle.member = value != 0; \
  }

#define PAGE_SETTING_FIELD_NESTED_BOOL(Suffix, member) \
  [[nodiscard]] static int \
  Get##Suffix(const Bundle &bundle) noexcept \
  { \
    return bundle.member ? 1 : 0; \
  } \
  static void \
  Set##Suffix(Bundle &bundle, int value) noexcept \
  { \
    bundle.member = value != 0; \
  }

#define PAGE_SETTING_FIELD_ENUM(Suffix, member, Type) \
  [[nodiscard]] static int \
  Get##Suffix(const Bundle &bundle) noexcept \
  { \
    return int(bundle.member); \
  } \
  static void \
  Set##Suffix(Bundle &bundle, int value) noexcept \
  { \
    bundle.member = Type(value); \
  }

#define PAGE_SETTING_FIELD_INT(Suffix, member) \
  [[nodiscard]] static int \
  Get##Suffix(const Bundle &bundle) noexcept \
  { \
    return bundle.member; \
  } \
  static void \
  Set##Suffix(Bundle &bundle, int value) noexcept \
  { \
    bundle.member = value; \
  }

#define PAGE_SETTING_FIELD_ROW(Suffix) \
  { Get##Suffix, Set##Suffix }
