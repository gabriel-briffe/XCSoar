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

static bool
ParseLayerTriple(const char *p, const char *end,
                 double &shape_nm, double &label_nm,
                 double &important_nm) noexcept
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

  return *endptr == '\0' || *endptr == ',';
}

static bool
ParseLayerEntry(const char *p, const char *end,
                const TopographyStore &store,
                StaticString<32> &layer_name,
                double &shape_nm, double &label_nm,
                double &important_nm) noexcept
{
  for (const auto &file : store) {
    const char *name = file.GetLayerName();
    const std::size_t name_len = strlen(name);
    if (p + name_len > end)
      continue;

    if (memcmp(p, name, name_len) != 0)
      continue;

    const char *triple = p + name_len;
    if (triple >= end)
      continue;

    /* Current format: layer:shape:label:important
       Legacy format (missing separator): layershape:label:important */
    if (*triple == ':')
      ++triple;
    else if (*triple < '0' || *triple > '9')
      continue;

    if (!ParseLayerTriple(triple, end, shape_nm, label_nm, important_nm))
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
    if (!ParseLayerEntry(p, end, store, layer_name,
                         shape_nm, label_nm, important_nm))
      break;

    if (TopographyFile *file = store.FindLayer(layer_name.c_str())) {
      file->SetThresholds(NmToThreshold(shape_nm),
                          NmToThreshold(label_nm),
                          NmToThreshold(important_nm));
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
    if (file.GetScaleThreshold() == file.GetDefaultScaleThreshold() &&
        file.GetLabelThreshold() == file.GetDefaultLabelThreshold() &&
        file.GetImportantLabelThreshold() ==
        file.GetDefaultImportantLabelThreshold())
      continue;

    any_custom = true;

    char triple[96];
    snprintf(triple, sizeof(triple), "%s%s:%.3f:%.3f:%.3f",
             first ? "" : ",",
             file.GetLayerName(),
             ThresholdToNm(file.GetScaleThreshold()),
             ThresholdToNm(file.GetLabelThreshold()),
             ThresholdToNm(file.GetImportantLabelThreshold()));
    first = false;
    buffer += triple;
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
  /* Thresholds from the full-resolution ALPS map (topology.tpl).  The
     repository download uses other layer names (e.g. water_area
     instead of water_area_large); map those explicitly. */
  static constexpr struct {
    const char *layer_name;
    double shape_nm, label_nm, important_nm;
  } preset[] = {
    { "city_area", 50, 50, 0 },
    { "water_area", 5, 5, 0 },
    { "water_area_large", 5, 5, 0 },
    { "water_area_small", 1, 1, 0 },
    { "water_line", 5, 5, 0 },
    { "roadbig_line", 15, 15, 0 },
    { "roadmedium_line", 8, 8, 0 },
    { "roadsmall_line", 2, 2, 0 },
    { "railway_line", 10, 10, 0 },
    { "city_point", 50, 50, 10 },
    { "town_point", 10, 10, 3 },
    { "suburb_point", 3, 3, 0 },
    { "village_point", 3, 3, 0 },
    { "airstrip_area", 10, 1, 1 },
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
