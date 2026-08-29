// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Topography/TopographySettings.hpp"
#include "Topography/TopographyStore.hpp"
#include "Topography/TopographyFile.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Current.hpp"
#include "Profile/Map.hpp"
#include "util/StringPointer.hxx"
#include "util/StringCompare.hxx"
#include "util/StaticString.hxx"

#include <cstdlib>
#include <cstring>

namespace {

constexpr double
NmToThreshold(double nm) noexcept
{
  return nm * 1000.;
}

constexpr double
ThresholdToNm(double threshold) noexcept
{
  return threshold / 1000.;
}

[[gnu::pure]]
const char *
GetConfiguredMapBase() noexcept
{
  const auto base = Profile::map.GetPathBase(ProfileKeys::MapFile);
  if (base == nullptr || base.empty())
    return nullptr;

  return base.c_str();
}

using LayerDisplayMode = TopographyFile::LayerDisplayMode;

[[gnu::pure]]
constexpr unsigned
DisplayModeToInt(LayerDisplayMode mode) noexcept
{
  return unsigned(mode);
}

[[gnu::pure]]
constexpr LayerDisplayMode
IntToDisplayMode(unsigned value) noexcept
{
  switch (value) {
  case 1:
    return LayerDisplayMode::GRAPHIC;
  case 2:
    return LayerDisplayMode::LABEL;
  case 3:
    return LayerDisplayMode::NONE;
  default:
    return LayerDisplayMode::BOTH;
  }
}

[[gnu::pure]]
bool
IsDefaultLayer(const TopographyFile &file) noexcept
{
  return file.GetScaleThreshold() == file.GetDefaultScaleThreshold() &&
         file.GetLabelThreshold() == file.GetDefaultLabelThreshold() &&
         file.GetImportantLabelThreshold() ==
         file.GetDefaultImportantLabelThreshold() &&
         file.GetDisplayMode() == LayerDisplayMode::BOTH;
}

static bool
ParseLayerFields(const char *p, const char *end,
                 double &shape_nm, double &label_nm,
                 double &important_nm,
                 LayerDisplayMode &mode) noexcept
{
  char *endptr;
  shape_nm = strtod(p, &endptr);
  if (endptr == p || *endptr != ':')
    return false;

  p = endptr + 1;
  label_nm = strtod(p, &endptr);
  if (endptr == p || *endptr != ':')
    return false;

  p = endptr + 1;
  important_nm = strtod(p, &endptr);
  if (endptr == p)
    return false;

  if (endptr > end)
    return false;

  mode = LayerDisplayMode::BOTH;
  if (*endptr == ':') {
    p = endptr + 1;
    const unsigned mode_value = strtoul(p, &endptr, 10);
    if (endptr == p)
      return false;
    if (endptr > end)
      return false;
    mode = IntToDisplayMode(mode_value);
  }

  return *endptr == '\0' || *endptr == ',';
}

static bool
ParseLayerEntry(const char *p, const char *end,
                const TopographyStore &store,
                StaticString<32> &layer_name,
                double &shape_nm, double &label_nm,
                double &important_nm,
                LayerDisplayMode &mode) noexcept
{
  for (const auto &file : store) {
    const char *name = file.GetLayerName();
    const std::size_t name_len = strlen(name);
    if (p + name_len > end)
      continue;

    if (memcmp(p, name, name_len) != 0)
      continue;

    const char *fields = p + name_len;
    if (fields >= end)
      continue;

    /* Current format: layer:shape:label:important[:mode]
       Legacy format (missing separator): layershape:label:important */
    if (*fields == ':')
      ++fields;
    else if (*fields < '0' || *fields > '9')
      continue;

    if (!ParseLayerFields(fields, end, shape_nm, label_nm, important_nm,
                          mode))
      continue;

    layer_name = name;
    return true;
  }

  return false;
}

} // namespace

void
TopographySettings::ApplyToStore(TopographyStore &store) noexcept
{
  const char *map_base = GetConfiguredMapBase();
  if (map_base == nullptr)
    return;

  const char *value = Profile::Get(ProfileKeys::TopographyLayerOverrides);
  if (value == nullptr || *value == '\0')
    return;

  const char *sep = strchr(value, '|');
  if (sep == nullptr)
    return;

  StaticString<64> stored_map;
  stored_map = std::string_view{value, std::size_t(sep - value)};
  if (!StringIsEqual(stored_map.c_str(), map_base))
    return;

  const char *p = sep + 1;
  bool any_applied = false;
  while (*p != '\0') {
    const char *comma = strchr(p, ',');
    const char *end = comma != nullptr ? comma : p + strlen(p);

    StaticString<32> layer_name;
    double shape_nm, label_nm, important_nm;
    LayerDisplayMode mode;
    if (!ParseLayerEntry(p, end, store, layer_name,
                         shape_nm, label_nm, important_nm, mode))
      break;

    if (TopographyFile *file = store.FindLayer(layer_name.c_str())) {
      file->SetThresholds(NmToThreshold(shape_nm),
                          NmToThreshold(label_nm),
                          NmToThreshold(important_nm));
      file->SetDisplayMode(mode);
      any_applied = true;
    }

    if (comma == nullptr)
      break;

    p = comma + 1;
  }

  if (any_applied)
    store.NotifyThresholdsChanged();
}

void
TopographySettings::SaveFromStore(const TopographyStore &store) noexcept
{
  const char *map_base = GetConfiguredMapBase();
  if (map_base == nullptr)
    return;

  StaticString<1024> buffer;
  buffer = map_base;
  buffer += "|";

  bool any_custom = false;
  bool first = true;

  for (const auto &file : store) {
    if (IsDefaultLayer(file))
      continue;

    any_custom = true;

    char entry[112];
    snprintf(entry, sizeof(entry), "%s%s:%.3f:%.3f:%.3f:%u",
             first ? "" : ",",
             file.GetLayerName(),
             ThresholdToNm(file.GetScaleThreshold()),
             ThresholdToNm(file.GetLabelThreshold()),
             ThresholdToNm(file.GetImportantLabelThreshold()),
             DisplayModeToInt(file.GetDisplayMode()));
    first = false;
    buffer += entry;
  }

  if (!any_custom) {
    Profile::Set(ProfileKeys::TopographyLayerOverrides, "");
    Profile::Save();
    return;
  }

  Profile::Set(ProfileKeys::TopographyLayerOverrides, buffer.c_str());
  Profile::Save();
}

unsigned
TopographySettings::ApplyCustomPreset(TopographyStore &store) noexcept
{
  const char *map_base = GetConfiguredMapBase();
  if (map_base == nullptr ||
      !StringIsEqual(map_base, "ALPS_Test.xcm"))
    return 0;

  /* Hard-coded thresholds for ALPS_Test.xcm (shape / label /
     important only; display mode is left unchanged). */
  static constexpr struct {
    const char *layer_name;
    double shape_nm, label_nm, important_nm;
  } preset[] = {
    { "city_area_large", 12.500, 0.000, 0.000 },
    { "city_area_small", 1.875, 0.000, 0.000 },
    { "water_area_large", 18.750, 1.875, 0.000 },
    { "water_area_small", 2.500, 0.625, 0.000 },
    { "water_lines", 15.000, 8.000, 0.000 },
    { "roadbig_line", 12.500, 15.000, 0.000 },
    { "city_point", 11.250, 25.000, 0.000 },
    { "town_point", 10.000, 10.000, 0.000 },
    { "suburb_point", 2.500, 2.500, 0.000 },
    { "building_area_large", 1.250, 0.000, 0.000 },
    { "aerodrome_area", 8.000, 8.000, 0.000 },
    { "airstrip_area", 10.000, 1.000, 0.000 },
    { "peak_point_high", 2.500, 1.250, 1.250 },
    { "peak_point", 1.250, 0.625, 0.000 },
    { "pass_point_high", 2.500, 1.250, 1.250 },
    { "pass_point", 1.250, 0.625, 0.000 },
  };

  unsigned count = 0;
  for (const auto &entry : preset) {
    TopographyFile *file = store.FindLayer(entry.layer_name);
    if (file == nullptr)
      continue;

    file->SetThresholds(NmToThreshold(entry.shape_nm),
                        NmToThreshold(entry.label_nm),
                        NmToThreshold(entry.important_nm));
    ++count;
  }

  if (count > 0)
    store.NotifyThresholdsChanged();

  return count;
}
