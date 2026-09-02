// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AirspaceCRendererSettingsPanel.hpp"
#include "Airspace.hpp"
#include "../ColorListDialog.hpp"
#include "../WidgetDialog.hpp"
#include "Airspace/AirspaceClassColorProfile.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "PageSetting.hpp"
#include "PageSettings.hpp"
#include "Profile/AirspaceConfig.hpp"
#include "Profile/Current.hpp"
#include "Profile/PageProfile.hpp"
#include "Profile/Profile.hpp"
#include "ui/canvas/Features.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "UIState.hpp"
#include "UIGlobals.hpp"
#include "Look/Look.hpp"

#include <cassert>

namespace {

void
SaveClassFillColor(AirspaceClass type, const RGB8Color &color,
                   const PageAirspaceRendererSettingsContext &page_context,
                   bool &changed) noexcept
{
  const PageSettingId id = AirspaceDisplaySetting::FillColorId(type);
  const int packed = AirspaceClassColorProfile::Pack(color);

  if (page_context.overrides != nullptr) {
    page_context.overrides->SetValue(id, packed);
    changed = true;
    return;
  }

  const PagesState &state = CommonInterface::GetUIState().pages;

  if (!state.special_page.IsDefined()) {
    auto &overrides =
      CommonInterface::SetUISettings().pages.overrides[state.current_index];
    if (const int *value = overrides.FindValue(id);
        value != nullptr && *value != PageSettingOverrides::INHERIT) {
      overrides.SetValue(id, packed);
      Profile::Save(Profile::map, overrides, state.current_index);
      changed = true;
      return;
    }
  }

  Profile::SetAirspaceFillColor(Profile::map, type, color);
  changed = true;
}

void
SaveClassBorderColor(AirspaceClass type, const RGB8Color &color,
                     const PageAirspaceRendererSettingsContext &page_context,
                     bool &changed) noexcept
{
  const PageSettingId id = AirspaceDisplaySetting::BorderColorId(type);
  const int packed = AirspaceClassColorProfile::Pack(color);

  if (page_context.overrides != nullptr) {
    page_context.overrides->SetValue(id, packed);
    changed = true;
    return;
  }

  const PagesState &state = CommonInterface::GetUIState().pages;

  if (!state.special_page.IsDefined()) {
    auto &overrides =
      CommonInterface::SetUISettings().pages.overrides[state.current_index];
    if (const int *value = overrides.FindValue(id);
        value != nullptr && *value != PageSettingOverrides::INHERIT) {
      overrides.SetValue(id, packed);
      Profile::Save(Profile::map, overrides, state.current_index);
      changed = true;
      return;
    }
  }

  Profile::SetAirspaceBorderColor(Profile::map, type, color);
  changed = true;
}

} // namespace

AirspaceClassRendererSettingsPanel::AirspaceClassRendererSettingsPanel(
  AirspaceClass _type,
  PageAirspaceRendererSettingsContext _page_context) noexcept
  :RowFormWidget(UIGlobals::GetDialogLook()), border_color_changed(false),
   fill_color_changed(false), fill_brush_changed(false), type(_type),
   page_context(_page_context)
{
  assert(type < AIRSPACECLASSCOUNT);
}

void
AirspaceClassRendererSettingsPanel::Prepare(ContainerWindow &parent,
                                            const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  settings = CommonInterface::GetMapSettings().airspace.classes[type];

  if (page_context.overrides != nullptr) {
    if (const int *value =
          page_context.overrides->FindValue(
            AirspaceDisplaySetting::FillColorId(type));
        value != nullptr)
      settings.fill_color = AirspaceClassColorProfile::Unpack(*value);

    if (const int *value =
          page_context.overrides->FindValue(
            AirspaceDisplaySetting::BorderColorId(type));
        value != nullptr)
      settings.border_color = AirspaceClassColorProfile::Unpack(*value);
  }

  AddButton(_("Change Border Color"), [this](){
    border_color_changed |= ShowColorListDialog(settings.border_color);
  });

  AddButton(_("Change Fill Color"), [this](){
    fill_color_changed |= ShowColorListDialog(settings.fill_color);
  });

#ifdef HAVE_HATCHED_BRUSH
#ifdef HAVE_ALPHA_BLEND
  bool transparency = CommonInterface::GetMapSettings().airspace.transparency;
  if (!transparency)
#endif
    AddButton(_("Change Fill Brush"), [this](){
      int pattern_index =
        dlgAirspacePatternsShowModal(UIGlobals::GetLook().map.airspace);

      if (pattern_index >= 0 && pattern_index != settings.brush) {
        settings.brush = pattern_index;
        fill_brush_changed = true;
      }
    });
#ifdef HAVE_ALPHA_BLEND
  else
    AddDummy();
#endif
#else
  AddDummy();
#endif

  AddInteger(_("Border Width"),
             _("The width of the border drawn around each airspace. "
               "Set this value to zero to hide the border."),
             "%d", "%d", 0, 5, 1, settings.border_width);

  static constexpr StaticEnumChoice fill_mode_list[] = {
    { AirspaceClassRendererSettings::FillMode::ALL, N_("Filled"), },
    { AirspaceClassRendererSettings::FillMode::PADDING, N_("Only padding"), },
    { AirspaceClassRendererSettings::FillMode::NONE, N_("Not filled"), },
    nullptr
  };

  AddEnum(_("Fill Mode"),
          _("Defines how the airspace is filled with the configured color."),
          fill_mode_list, (unsigned)settings.fill_mode);
}

bool
AirspaceClassRendererSettingsPanel::Save(bool &changed) noexcept
{
  if (border_color_changed)
    SaveClassBorderColor(type, settings.border_color, page_context, changed);

  if (fill_color_changed)
    SaveClassFillColor(type, settings.fill_color, page_context, changed);

  if (page_context.overrides == nullptr) {
#ifdef HAVE_HATCHED_BRUSH
    if (fill_brush_changed) {
      Profile::SetAirspaceBrush(Profile::map, type, settings.brush);
      changed = true;
    }
#endif

    if (SaveValueInteger(BorderWidth, settings.border_width)) {
      Profile::SetAirspaceBorderWidth(Profile::map, type, settings.border_width);
      changed = true;
    }

    if (SaveValueEnum(FillMode, settings.fill_mode)) {
      Profile::SetAirspaceFillMode(Profile::map, type,
                                   (unsigned)settings.fill_mode);
      changed = true;
    }
  }

  if (!changed)
    return true;

  if (page_context.overrides != nullptr) {
    /* Page draft only — do not mutate live MapSettings (Pages panel
       applies overrides on save / page switch). */
    Profile::Save(Profile::map, *page_context.overrides,
                  page_context.page_index);
  } else {
    CommonInterface::SetMapSettings().airspace.classes[type] = settings;
  }

  return true;
}
