// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Airspace/AirspaceDisplaySetting.hpp"

#include "Airspace/AirspaceDisplayChoices.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingFieldAccessors.hpp"
#include "PageSettingModuleImpl.hpp"
#include "PageSettingProfile.hpp"
#include "Profile/AirspaceConfig.hpp"
#include "Profile/Current.hpp"
#include "Profile/Keys.hpp"
#include "util/Macros.hpp"

#include <cassert>
#include <cstdint>

namespace AirspaceDisplaySetting {

void ReadLive(Bundle &) noexcept;
void ApplyLive(const Bundle &) noexcept;

namespace {

using Bundle = AirspaceDisplaySetting::Bundle;

static constexpr PageSettingDescriptor catalog[] = {
  PageSettingCatalog::CatalogEnum(
    PageSettingId::AIRSPACE_DISPLAY,
    N_("Airspace display"),
    N_("Controls filtering of airspace for display and warnings. The "
       "airspace filter button also allows filtering of display and "
       "warnings independently for each airspace class."),
    "OverrideAirspaceDisplay",
    ProfileKeys::AltMode,
    {.airspace = AirspaceBundleField::DISPLAY},
    ProfileWireFormat::UINT8_ENUM,
    int(AirspaceDisplayMode::ALLON),
    airspace_display_mode_choices),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::AIRSPACE_LABEL_VISIBILITY,
    N_("Label visibility"),
    N_("Determines what labels are displayed."),
    "OverrideAirspaceLabelVisibility",
    ProfileKeys::AirspaceLabelSelection,
    {.airspace = AirspaceBundleField::LABEL_VISIBILITY},
    ProfileWireFormat::UINT8_ENUM,
    int(AirspaceRendererSettings::LabelSelection::NONE),
    airspace_label_visibility_choices),
  PageSettingCatalog::CatalogBool(
    PageSettingId::AIRSPACE_SHOW_NOTAM_LABELS,
    N_("Show NOTAM labels"),
    N_("Show brief NOTAM text labels on the map when zoomed in "
       "sufficiently."),
    "OverrideAirspaceShowNotamLabels",
    ProfileKeys::AirspaceShowNOTAMLabels,
    {.airspace = AirspaceBundleField::SHOW_NOTAM_LABELS}, 0),
  PageSettingCatalog::CatalogBool(
    PageSettingId::AIRSPACE_BLACK_OUTLINE,
    N_("Use black outline"),
    N_("Draw a black outline around each airspace rather than the "
       "airspace color."),
    "OverrideAirspaceBlackOutline",
    ProfileKeys::AirspaceBlackOutline,
    {.airspace = AirspaceBundleField::BLACK_OUTLINE}, 0),
  PageSettingCatalog::CatalogEnum(
    PageSettingId::AIRSPACE_FILL_MODE,
    N_("Airspace fill mode"),
    N_("Specifies the mode for filling the airspace area."),
    "OverrideAirspaceFillMode",
    ProfileKeys::AirspaceFillMode,
    {.airspace = AirspaceBundleField::FILL_MODE},
    ProfileWireFormat::UINT8_ENUM,
    int(AirspaceRendererSettings::FillMode::DEFAULT),
    airspace_fill_mode_choices),
};

static_assert(ARRAY_SIZE(catalog) == PageSettingAirspaceCount,
              "Catalog size must match airspace PageSettingId count");

struct FieldAccessor {
  int (*get)(const Bundle &) noexcept;
  void (*set)(Bundle &, int) noexcept;
};

PAGE_SETTING_FIELD_ENUM(Display, airspace.altitude_mode, AirspaceDisplayMode)
PAGE_SETTING_FIELD_ENUM(LabelVisibility, airspace.label_selection,
                          AirspaceRendererSettings::LabelSelection)
PAGE_SETTING_FIELD_BOOL(ShowNotamLabels, airspace.show_notam_labels)
PAGE_SETTING_FIELD_BOOL(BlackOutline, airspace.black_outline)
PAGE_SETTING_FIELD_ENUM(FillMode, airspace.fill_mode,
                          AirspaceRendererSettings::FillMode)

static constexpr FieldAccessor field_accessors[] = {
  PAGE_SETTING_FIELD_ROW(Display),
  PAGE_SETTING_FIELD_ROW(LabelVisibility),
  PAGE_SETTING_FIELD_ROW(ShowNotamLabels),
  PAGE_SETTING_FIELD_ROW(BlackOutline),
  PAGE_SETTING_FIELD_ROW(FillMode),
};

static_assert(ARRAY_SIZE(field_accessors) ==
              unsigned(AirspaceBundleField::COUNT),
              "Airspace field accessors must match AirspaceBundleField::COUNT");

[[nodiscard]]
AirspaceBundleField
FieldFromDescriptor(const PageSettingDescriptor &desc) noexcept
{
  return desc.bundle_field.airspace;
}

[[nodiscard]]
int
GetBundleField(const Bundle &bundle, AirspaceBundleField field) noexcept
{
  return field_accessors[unsigned(field)].get(bundle);
}

void
SetBundleField(Bundle &bundle, AirspaceBundleField field,
               int value) noexcept
{
  field_accessors[unsigned(field)].set(bundle, value);
}

using Impl = PageSettingModuleImpl::Module<
  Bundle, AirspaceBundleField, catalog, PageSettingAirspaceCount,
  PageSettingAirspaceStart, FieldFromDescriptor,
  GetBundleField, SetBundleField, ReadLive, ApplyLive>;

} // namespace

unsigned
Count() noexcept
{
  return Impl::Count();
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  return Impl::Get(id);
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  return Impl::Get(index);
}

bool
IsValidValue(PageSettingId id, int value) noexcept
{
  return Impl::IsValidValue(id, value);
}

int
GetLive(PageSettingId id) noexcept
{
  return Impl::GetLive(id);
}

void
SetLive(PageSettingId id, int value) noexcept
{
  Impl::SetLive(id, value);
}

int
LoadGlobal(PageSettingId id) noexcept
{
  return Impl::LoadGlobal(id);
}

void
SaveGlobal(PageSettingId id, int value) noexcept
{
  Impl::SaveGlobal(id, value);
}

int
GetValue(const Bundle &bundle, PageSettingId id) noexcept
{
  return Impl::GetValue(bundle, id);
}

void
SetValue(Bundle &bundle, PageSettingId id, int value) noexcept
{
  Impl::SetValue(bundle, id, value);
}

void
ReadLive(Bundle &bundle) noexcept
{
  bundle.airspace = CommonInterface::GetMapSettings().airspace;
}

void
ApplyLive(const Bundle &bundle) noexcept
{
  /* Preserve class filter/colours and other fields not in the catalog. */
  auto &live = CommonInterface::SetMapSettings().airspace;
  live.altitude_mode = bundle.airspace.altitude_mode;
  live.label_selection = bundle.airspace.label_selection;
  live.show_notam_labels = bundle.airspace.show_notam_labels;
  live.black_outline = bundle.airspace.black_outline;
  live.fill_mode = bundle.airspace.fill_mode;
}

void
LoadGlobal(Bundle &bundle) noexcept
{
  bundle.airspace.SetDefaults();
  Profile::Load(Profile::map, bundle.airspace);
}

bool
SaveGlobal(const Bundle &current, const Bundle &initial) noexcept
{
  return Impl::SaveGlobalBundle(current, initial);
}

} // namespace AirspaceDisplaySetting
