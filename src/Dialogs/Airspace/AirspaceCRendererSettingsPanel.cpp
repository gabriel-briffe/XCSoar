// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AirspaceCRendererSettingsPanel.hpp"
#include "Airspace.hpp"
#include "../ColorListDialog.hpp"
#include "../WidgetDialog.hpp"
#include "Airspace/AirspaceClassColorProfile.hpp"
#include "Airspace/AirspaceDisplayChoices.hpp"
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

/**
 * Persist @p value for @p id: draft page overrides, else current-page
 * override if present, else @p save_global.
 */
template<typename SaveGlobalFn>
void
SavePageOrGlobal(PageSettingId id, int value,
                 const PageAirspaceRendererSettingsContext &page_context,
                 bool &changed, SaveGlobalFn save_global) noexcept
{
  if (page_context.overrides != nullptr) {
    /* Draft only — Pages panel persists overrides on its own save. */
    page_context.overrides->SetValue(id, value);
    changed = true;
    return;
  }

  const PagesState &state = CommonInterface::GetUIState().pages;

  if (!state.special_page.IsDefined()) {
    auto &overrides =
      CommonInterface::SetUISettings().pages.overrides[state.current_index];
    if (const int *existing = overrides.FindValue(id);
        existing != nullptr && *existing != PageSettingOverrides::INHERIT) {
      overrides.SetValue(id, value);
      Profile::Save(Profile::map, overrides, state.current_index);
      changed = true;
      return;
    }
  }

  save_global();
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

    if (const int *value =
          page_context.overrides->FindValue(
            AirspaceDisplaySetting::BorderWidthId(type));
        value != nullptr)
      settings.border_width = unsigned(*value);

    if (const int *value =
          page_context.overrides->FindValue(
            AirspaceDisplaySetting::FillModeId(type));
        value != nullptr)
      settings.fill_mode =
        AirspaceClassRendererSettings::FillMode(*value);
  }

  AddButton(_("Change Border Color"), [this](){
    border_color_changed |= ShowColorListDialog(settings.border_color);
  });

  AddButton(_("Change Fill Color"), [this](){
    fill_color_changed |= ShowColorListDialog(settings.fill_color);
  });

  /* Fill brush stays global; page drafts do not override hatched brushes. */
  if (page_context.overrides == nullptr) {
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
  } else
    AddDummy();

  AddInteger(_("Border Width"),
             _("The width of the border drawn around each airspace. "
               "Set this value to zero to hide the border."),
             "%d", "%d", 0, 5, 1, settings.border_width);

  AddEnum(_("Fill Mode"),
          _("Defines how the airspace is filled with the configured color."),
          airspace_class_fill_mode_choices, (unsigned)settings.fill_mode);
}

bool
AirspaceClassRendererSettingsPanel::Save(bool &changed) noexcept
{
  if (border_color_changed)
    SavePageOrGlobal(AirspaceDisplaySetting::BorderColorId(type),
                     AirspaceClassColorProfile::Pack(settings.border_color),
                     page_context, changed,
                     [this]() {
                       Profile::SetAirspaceBorderColor(Profile::map, type,
                                                       settings.border_color);
                     });

  if (fill_color_changed)
    SavePageOrGlobal(AirspaceDisplaySetting::FillColorId(type),
                     AirspaceClassColorProfile::Pack(settings.fill_color),
                     page_context, changed,
                     [this]() {
                       Profile::SetAirspaceFillColor(Profile::map, type,
                                                     settings.fill_color);
                     });

  if (SaveValueInteger(BorderWidth, settings.border_width))
    SavePageOrGlobal(AirspaceDisplaySetting::BorderWidthId(type),
                     int(settings.border_width), page_context, changed,
                     [this]() {
                       Profile::SetAirspaceBorderWidth(Profile::map, type,
                                                       settings.border_width);
                     });

  if (SaveValueEnum(FillMode, settings.fill_mode))
    SavePageOrGlobal(AirspaceDisplaySetting::FillModeId(type),
                     int(settings.fill_mode), page_context, changed,
                     [this]() {
                       Profile::SetAirspaceFillMode(Profile::map, type,
                                                    uint8_t(settings.fill_mode));
                     });

  if (page_context.overrides == nullptr) {
#ifdef HAVE_HATCHED_BRUSH
    if (fill_brush_changed) {
      Profile::SetAirspaceBrush(Profile::map, type, settings.brush);
      changed = true;
    }
#endif

    if (changed)
      CommonInterface::SetMapSettings().airspace.classes[type] = settings;
  }

  return true;
}
