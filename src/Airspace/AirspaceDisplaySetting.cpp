// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Airspace/AirspaceDisplaySetting.hpp"

#include "Airspace/AirspaceClassFilterProfile.hpp"
#include "Airspace/AirspaceDisplayChoices.hpp"
#include "Formatter/AirspaceFormatter.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "PageSetting.hpp"
#include "PageSettingCatalog.hpp"
#include "PageSettingFieldAccessors.hpp"
#include "PageSettingFilterCatalog.hpp"
#include "PageSettingModuleImpl.hpp"
#include "PageSettingProfile.hpp"
#include "Profile/AirspaceConfig.hpp"
#include "Profile/Current.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UISettings.hpp"
#include "ui/canvas/Features.hpp"
#include "util/Macros.hpp"
#include "util/StringFormat.hpp"

#include <cassert>
#include <cstdint>

namespace AirspaceDisplaySetting {

void ReadLive(Bundle &) noexcept;
void ApplyLive(const Bundle &) noexcept;

namespace {

using Bundle = AirspaceDisplaySetting::Bundle;
using Duration = AirspaceWarningConfig::Duration;

static_assert(PageSettingAirspaceBaseCount == 13,
              "Airspace base catalog size mismatch");
static_assert(PageSettingAirspaceCount ==
              PageSettingAirspaceBaseCount +
              PageSettingAirspaceClassFilterCount,
              "Airspace catalog size mismatch");

static constexpr PageSettingDescriptor base_catalog[] = {
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
  PageSettingCatalog::CatalogInteger(
    PageSettingId::AIRSPACE_CLIP_ALTITUDE,
    N_("Clip altitude"),
    N_("For clip airspace mode, this is the altitude below which airspace "
       "is displayed."),
    "OverrideAirspaceClipAltitude",
    ProfileKeys::ClipAlt,
    {.airspace = AirspaceBundleField::CLIP_ALTITUDE},
    ProfileWireFormat::INT, 1000, 0, 20000, 100, "%d m"),
  PageSettingCatalog::CatalogInteger(
    PageSettingId::AIRSPACE_MARGIN,
    N_("Margin"),
    N_("For auto and all below airspace mode, this is the altitude "
       "above/below which airspace is included."),
    "OverrideAirspaceMargin",
    ProfileKeys::AltMargin,
    {.airspace = AirspaceBundleField::MARGIN},
    ProfileWireFormat::INT, 100, 0, 10000, 100, "%d m"),
  PageSettingCatalog::CatalogBool(
    PageSettingId::AIRSPACE_WARNINGS,
    N_("Warnings"),
    N_("Enable/disable all airspace warnings."),
    "OverrideAirspaceWarnings",
    ProfileKeys::AirspaceWarning,
    {.airspace = AirspaceBundleField::WARNINGS}),
  PageSettingCatalog::CatalogBool(
    PageSettingId::AIRSPACE_WARNING_DIALOG,
    N_("Warnings dialog"),
    N_("Enable/disable displaying airspaces warnings dialog."),
    "OverrideAirspaceWarningDialog",
    ProfileKeys::AirspaceWarningDialog,
    {.airspace = AirspaceBundleField::WARNING_DIALOG}),
  PageSettingCatalog::CatalogInteger(
    PageSettingId::AIRSPACE_WARNING_TIME,
    N_("Warning time"),
    N_("This is the time before an airspace incursion is estimated at "
       "which the system will warn the pilot."),
    "OverrideAirspaceWarningTime",
    ProfileKeys::WarningTime,
    {.airspace = AirspaceBundleField::WARNING_TIME},
    ProfileWireFormat::INT, 30, 10, 1000, 5, "%d s"),
  PageSettingCatalog::CatalogBool(
    PageSettingId::AIRSPACE_REPETITIVE_SOUND,
    N_("Repetitive sound"),
    N_("Enable/disable repetitive warning sound when airspaces warnings "
       "dialog is displayed."),
    "OverrideAirspaceRepetitiveSound",
    ProfileKeys::RepetitiveSound,
    {.airspace = AirspaceBundleField::REPETITIVE_SOUND}, 0),
  PageSettingCatalog::CatalogInteger(
    PageSettingId::AIRSPACE_ACKNOWLEDGE_TIME,
    N_("Acknowledge time"),
    N_("This is the time period in which an acknowledged airspace warning "
       "will not be repeated."),
    "OverrideAirspaceAcknowledgeTime",
    ProfileKeys::AcknowledgementTime,
    {.airspace = AirspaceBundleField::ACKNOWLEDGE_TIME},
    ProfileWireFormat::INT, 30, 10, 1000, 5, "%d s"),
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
  PageSettingCatalog::CatalogBool(
    PageSettingId::AIRSPACE_TRANSPARENCY,
    N_("Airspace transparency"),
    N_("If enabled, then airspaces are filled transparently."),
    "OverrideAirspaceTransparency",
    ProfileKeys::AirspaceTransparency,
    {.airspace = AirspaceBundleField::TRANSPARENCY}, 0),
};

static_assert(ARRAY_SIZE(base_catalog) == PageSettingAirspaceBaseCount,
              "Base catalog size must match PageSettingAirspaceBaseCount");

struct FieldAccessor {
  int (*get)(const Bundle &) noexcept;
  void (*set)(Bundle &, int) noexcept;
};

PAGE_SETTING_FIELD_ENUM(Display, airspace.altitude_mode, AirspaceDisplayMode)
PAGE_SETTING_FIELD_ENUM(LabelVisibility, airspace.label_selection,
                          AirspaceRendererSettings::LabelSelection)
PAGE_SETTING_FIELD_BOOL(ShowNotamLabels, airspace.show_notam_labels)
PAGE_SETTING_FIELD_INT(ClipAltitude, airspace.clip_altitude)
PAGE_SETTING_FIELD_INT(Margin, computer.warnings.altitude_warning_margin)
PAGE_SETTING_FIELD_BOOL(Warnings, computer.enable_warnings)
PAGE_SETTING_FIELD_BOOL(WarningDialog, enable_airspace_warning_dialog)

[[nodiscard]] static int
GetWarningTime(const Bundle &bundle) noexcept
{
  return int(bundle.computer.warnings.warning_time.count());
}

static void
SetWarningTime(Bundle &bundle, int value) noexcept
{
  bundle.computer.warnings.warning_time = Duration{unsigned(value)};
}

PAGE_SETTING_FIELD_BOOL(RepetitiveSound, computer.warnings.repetitive_sound)

[[nodiscard]] static int
GetAcknowledgeTime(const Bundle &bundle) noexcept
{
  return int(bundle.computer.warnings.acknowledgement_time.count());
}

static void
SetAcknowledgeTime(Bundle &bundle, int value) noexcept
{
  bundle.computer.warnings.acknowledgement_time = Duration{unsigned(value)};
}

PAGE_SETTING_FIELD_BOOL(BlackOutline, airspace.black_outline)
PAGE_SETTING_FIELD_ENUM(FillMode, airspace.fill_mode,
                          AirspaceRendererSettings::FillMode)
PAGE_SETTING_FIELD_BOOL(Transparency, transparency)

static constexpr FieldAccessor field_accessors[] = {
  PAGE_SETTING_FIELD_ROW(Display),
  PAGE_SETTING_FIELD_ROW(LabelVisibility),
  PAGE_SETTING_FIELD_ROW(ShowNotamLabels),
  PAGE_SETTING_FIELD_ROW(ClipAltitude),
  PAGE_SETTING_FIELD_ROW(Margin),
  PAGE_SETTING_FIELD_ROW(Warnings),
  PAGE_SETTING_FIELD_ROW(WarningDialog),
  PAGE_SETTING_FIELD_ROW(WarningTime),
  PAGE_SETTING_FIELD_ROW(RepetitiveSound),
  PAGE_SETTING_FIELD_ROW(AcknowledgeTime),
  PAGE_SETTING_FIELD_ROW(BlackOutline),
  PAGE_SETTING_FIELD_ROW(FillMode),
  PAGE_SETTING_FIELD_ROW(Transparency),
};

static_assert(ARRAY_SIZE(field_accessors) ==
              unsigned(AirspaceBundleField::COUNT),
              "Airspace field accessors must match AirspaceBundleField::COUNT");

static char filter_override_keys[PageSettingAirspaceClassFilterCount][40];
static PageSettingDescriptor catalog[PageSettingAirspaceCount];
static PageSettingId
  filter_dialog_order[PageSettingAirspaceClassFilterCount];
static bool catalog_ready = false;

[[nodiscard]]
const char *
FilterDialogLabel(PageSettingId id) noexcept
{
  return PageSettingCatalog::GettextOptional(
    catalog[unsigned(id) - PageSettingAirspaceStart].label);
}

void
InitFilterDialogOrder() noexcept
{
  PageSettingFilterCatalog::FillConsecutiveIds(
    filter_dialog_order,
    PageSettingId::AIRSPACE_CLASS_FILTER_BEGIN,
    PageSettingAirspaceClassFilterCount);

  PageSettingFilterCatalog::InitSortedOrder(
    filter_dialog_order, FilterDialogRowCount(), FilterDialogLabel);
}

void
EnsureCatalog() noexcept
{
  if (catalog_ready)
    return;

  PageSettingFilterCatalog::CopyBase(catalog, base_catalog,
                                     PageSettingAirspaceBaseCount);

  for (unsigned i = 0; i < PageSettingAirspaceClassFilterCount; ++i) {
    const AirspaceClass cls = AirspaceClass(i + 1);
    const PageSettingId id =
      PageSettingId(unsigned(PageSettingId::AIRSPACE_CLASS_FILTER_BEGIN) + i);

    StringFormat(filter_override_keys[i], sizeof(filter_override_keys[i]),
                 "OverrideAirspaceFilter%u", unsigned(cls));

    catalog[PageSettingAirspaceBaseCount + i] =
      PageSettingFilterCatalog::MakeEnumFilter(
        id,
        AirspaceFormatter::GetClass(cls),
        N_("Display and warning filter for this airspace class."),
        filter_override_keys[i],
        {.airspace = AirspaceBundleField::CLASS_FILTER},
        ProfileWireFormat::UINT8_ENUM,
        AirspaceClassFilterProfile::Encode(true, true),
        airspace_class_filter_mode_choices);
  }

  InitFilterDialogOrder();
  catalog_ready = true;
}

[[nodiscard]]
AirspaceBundleField
FieldFromDescriptor(const PageSettingDescriptor &desc) noexcept
{
  assert(unsigned(desc.bundle_field.airspace) <
         unsigned(AirspaceBundleField::COUNT));
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

[[nodiscard]]
int
GetClassFilterValue(const Bundle &bundle, AirspaceClass cls) noexcept
{
  assert(unsigned(cls) < AIRSPACECLASSCOUNT);
  return AirspaceClassFilterProfile::Encode(
    bundle.airspace.classes[unsigned(cls)].display,
    bundle.computer.warnings.class_warnings[unsigned(cls)]);
}

void
SetClassFilterValue(Bundle &bundle, AirspaceClass cls, int value) noexcept
{
  assert(unsigned(cls) < AIRSPACECLASSCOUNT);
  bundle.airspace.classes[unsigned(cls)].display =
    AirspaceClassFilterProfile::Display(value);
  bundle.computer.warnings.class_warnings[unsigned(cls)] =
    AirspaceClassFilterProfile::Warn(value);
}

using BaseImpl = PageSettingModuleImpl::Module<
  Bundle, AirspaceBundleField, catalog, PageSettingAirspaceBaseCount,
  PageSettingAirspaceStart, FieldFromDescriptor,
  GetBundleField, SetBundleField, ReadLive, ApplyLive>;

[[nodiscard]]
int
GetValueImpl(const Bundle &bundle, PageSettingId id) noexcept
{
  if (IsClassFilter(id))
    return GetClassFilterValue(bundle, ClassFromId(id));

  return BaseImpl::GetValue(bundle, id);
}

void
SetValueImpl(Bundle &bundle, PageSettingId id, int value) noexcept
{
  EnsureCatalog();
  if (!PageSettingCatalog::IsValidValue(
        catalog[unsigned(id) - PageSettingAirspaceStart], value))
    return;

  if (IsClassFilter(id)) {
    SetClassFilterValue(bundle, ClassFromId(id), value);
    return;
  }

  BaseImpl::SetValue(bundle, id, value);
}

[[nodiscard]]
int
LoadGlobalImpl(PageSettingId id) noexcept
{
  if (IsClassFilter(id))
    return AirspaceClassFilterProfile::Load(ClassFromId(id));

  EnsureCatalog();
  return PageSettingProfile::Load(
    catalog[unsigned(id) - PageSettingAirspaceStart]);
}

void
SaveGlobalImpl(PageSettingId id, int value) noexcept
{
  EnsureCatalog();
  if (!PageSettingCatalog::IsValidValue(
        catalog[unsigned(id) - PageSettingAirspaceStart], value))
    return;

  if (IsClassFilter(id)) {
    AirspaceClassFilterProfile::Save(ClassFromId(id), value);
    return;
  }

  PageSettingProfile::Save(
    catalog[unsigned(id) - PageSettingAirspaceStart], value);
}

using Dyn = PageSettingModuleImpl::DynamicModule<
  Bundle,
  PageSettingAirspaceCount,
  PageSettingAirspaceStart,
  unsigned(PageSettingId::COUNT),
  catalog,
  EnsureCatalog,
  GetValueImpl,
  SetValueImpl,
  LoadGlobalImpl,
  SaveGlobalImpl,
  ReadLive,
  ApplyLive>;

} // namespace

PageSettingId
FilterDialogRowId(unsigned row) noexcept
{
  assert(row < FilterDialogRowCount());
  EnsureCatalog();
  return filter_dialog_order[row];
}

unsigned
Count() noexcept
{
  return Dyn::Count();
}

const PageSettingDescriptor &
Get(PageSettingId id) noexcept
{
  return Dyn::Get(id);
}

const PageSettingDescriptor &
Get(unsigned index) noexcept
{
  return Dyn::Get(index);
}

bool
IsValidValue(PageSettingId id, int value) noexcept
{
  return Dyn::IsValidValue(id, value);
}

int
GetValue(const Bundle &bundle, PageSettingId id) noexcept
{
  return GetValueImpl(bundle, id);
}

void
SetValue(Bundle &bundle, PageSettingId id, int value) noexcept
{
  SetValueImpl(bundle, id, value);
}

int
GetLive(PageSettingId id) noexcept
{
  return Dyn::GetLive(id);
}

void
SetLive(PageSettingId id, int value) noexcept
{
  Dyn::SetLive(id, value);
}

int
LoadGlobal(PageSettingId id) noexcept
{
  return Dyn::LoadGlobal(id);
}

void
SaveGlobal(PageSettingId id, int value) noexcept
{
  Dyn::SaveGlobal(id, value);
}

void
ReadLive(Bundle &bundle) noexcept
{
  bundle.airspace = CommonInterface::GetMapSettings().airspace;
  bundle.computer = CommonInterface::GetComputerSettings().airspace;
  bundle.enable_airspace_warning_dialog =
    CommonInterface::GetUISettings().enable_airspace_warning_dialog;
#if defined(HAVE_HATCHED_BRUSH) && defined(HAVE_ALPHA_BLEND)
  bundle.transparency = bundle.airspace.transparency;
#else
  bundle.transparency = false;
#endif
}

void
ApplyLive(const Bundle &bundle) noexcept
{
  auto &live = CommonInterface::SetMapSettings().airspace;
  live.altitude_mode = bundle.airspace.altitude_mode;
  live.label_selection = bundle.airspace.label_selection;
  live.show_notam_labels = bundle.airspace.show_notam_labels;
  live.clip_altitude = bundle.airspace.clip_altitude;
  live.black_outline = bundle.airspace.black_outline;
  live.fill_mode = bundle.airspace.fill_mode;
#if defined(HAVE_HATCHED_BRUSH) && defined(HAVE_ALPHA_BLEND)
  live.transparency = bundle.transparency;
#endif

  for (unsigned i = 1; i < AIRSPACECLASSCOUNT; ++i)
    live.classes[i].display = bundle.airspace.classes[i].display;

  auto &computer = CommonInterface::SetComputerSettings().airspace;
  computer.enable_warnings = bundle.computer.enable_warnings;
  computer.warnings.altitude_warning_margin =
    bundle.computer.warnings.altitude_warning_margin;
  computer.warnings.warning_time = bundle.computer.warnings.warning_time;
  computer.warnings.repetitive_sound =
    bundle.computer.warnings.repetitive_sound;
  computer.warnings.acknowledgement_time =
    bundle.computer.warnings.acknowledgement_time;
  for (unsigned i = 1; i < AIRSPACECLASSCOUNT; ++i)
    computer.warnings.class_warnings[i] =
      bundle.computer.warnings.class_warnings[i];

  CommonInterface::SetUISettings().enable_airspace_warning_dialog =
    bundle.enable_airspace_warning_dialog;
}

void
LoadGlobal(Bundle &bundle) noexcept
{
  bundle.airspace.SetDefaults();
  Profile::Load(Profile::map, bundle.airspace);

  bundle.computer.SetDefaults();
  Profile::Load(Profile::map, bundle.computer);

  bundle.enable_airspace_warning_dialog = true;
  Profile::Get(ProfileKeys::AirspaceWarningDialog,
               bundle.enable_airspace_warning_dialog);

#if defined(HAVE_HATCHED_BRUSH) && defined(HAVE_ALPHA_BLEND)
  bundle.transparency = bundle.airspace.transparency;
#else
  bundle.transparency = false;
  Profile::Get(ProfileKeys::AirspaceTransparency, bundle.transparency);
#endif
}

bool
SaveGlobal(const Bundle &current, const Bundle &initial) noexcept
{
  return Dyn::SaveGlobalBundle(current, initial);
}

} // namespace AirspaceDisplaySetting
