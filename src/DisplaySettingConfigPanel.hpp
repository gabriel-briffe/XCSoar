// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "PageSettingDescriptor.hpp"

class DataFieldListener;
class RowFormWidget;

namespace DisplaySettingConfigPanel {

void
AddBooleanRow(RowFormWidget &form, const PageSettingDescriptor &desc,
              bool value, DataFieldListener *listener=nullptr) noexcept;

void
AddEnumRow(RowFormWidget &form, const PageSettingDescriptor &desc,
           unsigned value, DataFieldListener *listener=nullptr) noexcept;

void
AddIntegerRow(RowFormWidget &form, const PageSettingDescriptor &desc,
              int value, DataFieldListener *listener=nullptr) noexcept;

void
AddRow(RowFormWidget &form, const PageSettingDescriptor &desc, int value,
       DataFieldListener *listener=nullptr) noexcept;

} // namespace DisplaySettingConfigPanel
