// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageSettingCommand.hpp"

#include "Language/Language.hpp"
#include "util/Macros.hpp"

#include <cassert>

namespace {

static constexpr PageSettingCommandDescriptor commands[] = {
  { PageSettingId::WAYPOINT_LABEL_VISIBILITY, N_("Waypoint labels") },
  { PageSettingId::TRAIL_LENGTH, N_("Trail") },
  { PageSettingId::TERRAIN_ENABLE, N_("Terrain") },
  { PageSettingId::TOPOGRAPHY_ENABLE, N_("Topography") },
  { PageSettingId::AIRSPACE_ENABLE, N_("Airspace") },
  { PageSettingId::AIRSPACE_LABEL_VISIBILITY, N_("Airspace labels") },
  { PageSettingId::DISTANCE_RINGS, N_("Distance rings") },
  { PageSettingId::PAGE_ONLY_ZOOM, N_("Zoom") },
};

} // namespace

unsigned
PageSettingCommandCount() noexcept
{
  return ARRAY_SIZE(commands);
}

const PageSettingCommandDescriptor &
PageSettingCommandGet(unsigned index) noexcept
{
  assert(index < PageSettingCommandCount());
  return commands[index];
}

bool
PageSettingCommandIsKnown(PageSettingId id) noexcept
{
  return PageSettingCommandFind(id) != nullptr;
}

const PageSettingCommandDescriptor *
PageSettingCommandFind(PageSettingId id) noexcept
{
  for (unsigned i = 0; i < PageSettingCommandCount(); ++i)
    if (commands[i].id == id)
      return &commands[i];
  return nullptr;
}
