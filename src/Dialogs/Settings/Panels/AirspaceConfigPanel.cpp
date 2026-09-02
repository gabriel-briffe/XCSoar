// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AirspaceConfigPanel.hpp"
#include "Airspace/AirspaceDisplaySetting.hpp"
#include "ConfigPanel.hpp"
#include "DisplaySettingConfigPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Boolean.hpp"
#include "Form/DataField/Listener.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Dialogs/Airspace/Airspace.hpp"
#include "Profile/Keys.hpp"
#include "Language/Language.hpp"
#include "Airspace/AirspaceComputerSettings.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"
#include "ui/canvas/Features.hpp"
#include "Interface.hpp"
#include "PageSetting.hpp"
#include "UIGlobals.hpp"
#include "UtilsSettings.hpp"

#include <cassert>

using namespace std::chrono;

namespace {

enum ControlIndex : unsigned {
  DISPLAY = 0,
  LABEL_VISIBILITY,
  SHOW_NOTAM_LABELS,
  CLIP_ALTITUDE,
  ALT_WARNING_MARGIN,
  AIRSPACE_WARNINGS,
  WARNING_DIALOG,
  WARNING_TIME,
  REPETITIVE_SOUND,
  ACKNOWLEDGE_TIME,
  BLACK_OUTLINE,
  FILL_MODE,
#if defined(HAVE_HATCHED_BRUSH) && defined(HAVE_ALPHA_BLEND)
  TRANSPARENCY,
#endif
};

[[nodiscard]]
PageSettingId
CatalogIdForControl(unsigned control) noexcept
{
  switch (ControlIndex(control)) {
  case ControlIndex::DISPLAY:
    return PageSettingId::AIRSPACE_DISPLAY;
  case ControlIndex::LABEL_VISIBILITY:
    return PageSettingId::AIRSPACE_LABEL_VISIBILITY;
  case ControlIndex::SHOW_NOTAM_LABELS:
    return PageSettingId::AIRSPACE_SHOW_NOTAM_LABELS;
  case ControlIndex::BLACK_OUTLINE:
    return PageSettingId::AIRSPACE_BLACK_OUTLINE;
  case ControlIndex::FILL_MODE:
    return PageSettingId::AIRSPACE_FILL_MODE;
  default:
    assert(false);
    return PageSettingId::AIRSPACE_DISPLAY;
  }
}

} // namespace

class AirspaceConfigPanel final
  : public RowFormWidget, DataFieldListener {
  AirspaceDisplaySetting::Bundle bundle;
  AirspaceDisplaySetting::Bundle initial_bundle;

  void SyncCatalogFromForm() noexcept;

public:
  AirspaceConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void ShowDisplayControls(AirspaceDisplayMode mode);
  void ShowWarningControls(bool visible);

  /* methods from Widget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
  void Show(const PixelRect &rc) noexcept override;
  void Hide() noexcept override;

private:
  /* methods from DataFieldListener */
  void OnModified(DataField &df) noexcept override;
};

void
AirspaceConfigPanel::SyncCatalogFromForm() noexcept
{
  static constexpr unsigned catalog_controls[] = {
    unsigned(ControlIndex::DISPLAY),
    unsigned(ControlIndex::LABEL_VISIBILITY),
    unsigned(ControlIndex::SHOW_NOTAM_LABELS),
    unsigned(ControlIndex::BLACK_OUTLINE),
    unsigned(ControlIndex::FILL_MODE),
  };

  for (const unsigned control : catalog_controls) {
    const auto id = CatalogIdForControl(control);
    const auto &desc = AirspaceDisplaySetting::Get(id);

    switch (desc.type) {
    case PageSettingType::BOOL:
      AirspaceDisplaySetting::SetValue(bundle, id,
                                       GetValueBoolean(control) ? 1 : 0);
      break;

    case PageSettingType::ENUM:
      AirspaceDisplaySetting::SetValue(bundle, id,
                                       int(GetValueEnum(control)));
      break;

    case PageSettingType::INTEGER:
      AirspaceDisplaySetting::SetValue(bundle, id,
                                       GetValueInteger(control));
      break;
    }
  }
}

void
AirspaceConfigPanel::ShowDisplayControls(AirspaceDisplayMode mode)
{
  SetRowVisible(unsigned(ControlIndex::CLIP_ALTITUDE),
                mode == AirspaceDisplayMode::CLIP);

  SetRowVisible(unsigned(ControlIndex::ALT_WARNING_MARGIN),
                mode == AirspaceDisplayMode::AUTO ||
                mode == AirspaceDisplayMode::ALLBELOW);
}

void
AirspaceConfigPanel::ShowWarningControls(bool visible)
{
  SetRowVisible(unsigned(ControlIndex::WARNING_DIALOG), visible);
  SetRowVisible(unsigned(ControlIndex::WARNING_TIME), visible);
  SetRowVisible(unsigned(ControlIndex::REPETITIVE_SOUND), visible);
  SetRowVisible(unsigned(ControlIndex::ACKNOWLEDGE_TIME), visible);
}

void
AirspaceConfigPanel::Show(const PixelRect &rc) noexcept
{
  ConfigPanel::BorrowExtraButton(1, _("Colours"), [](){
    dlgAirspaceShowModal(true);
  });

  ConfigPanel::BorrowExtraButton(2, _("Filter"), [](){
    dlgAirspaceShowModal(false);
  });

  RowFormWidget::Show(rc);
}

void
AirspaceConfigPanel::Hide() noexcept
{
  RowFormWidget::Hide();
  ConfigPanel::ReturnExtraButton(1);
  ConfigPanel::ReturnExtraButton(2);
}

void
AirspaceConfigPanel::OnModified(DataField &df) noexcept
{
  if (IsDataField(unsigned(ControlIndex::DISPLAY), df)) {
    const DataFieldEnum &dfe = (const DataFieldEnum &)df;
    AirspaceDisplayMode mode = (AirspaceDisplayMode)dfe.GetValue();
    ShowDisplayControls(mode);
  } else if (IsDataField(unsigned(ControlIndex::AIRSPACE_WARNINGS), df)) {
    const DataFieldBoolean &dfb = (const DataFieldBoolean &)df;
    ShowWarningControls(dfb.GetValue());
  }
}

void
AirspaceConfigPanel::Prepare(ContainerWindow &parent,
                             const PixelRect &rc) noexcept
{
  const AirspaceComputerSettings &computer =
    CommonInterface::GetComputerSettings().airspace;
  const AirspaceRendererSettings &renderer =
    CommonInterface::GetMapSettings().airspace;
  const UISettings &ui_settings =
    CommonInterface::GetUISettings();

  RowFormWidget::Prepare(parent, rc);

  AirspaceDisplaySetting::ReadLive(bundle);

  auto AddCatalogRow = [this](PageSettingId id,
                              DataFieldListener *listener = nullptr) {
    const auto &desc = AirspaceDisplaySetting::Get(id);
    DisplaySettingConfigPanel::AddRow(
      *this, desc, AirspaceDisplaySetting::GetValue(bundle, id), listener);
  };

  AddCatalogRow(PageSettingId::AIRSPACE_DISPLAY, this);
  AddCatalogRow(PageSettingId::AIRSPACE_LABEL_VISIBILITY);
  SetExpertRow(unsigned(ControlIndex::LABEL_VISIBILITY));

  AddCatalogRow(PageSettingId::AIRSPACE_SHOW_NOTAM_LABELS);
  SetExpertRow(unsigned(ControlIndex::SHOW_NOTAM_LABELS));

  AddFloat(_("Clip altitude"),
           _("For clip airspace mode, this is the altitude below which airspace is displayed."),
           "%.0f %s", "%.0f", 0, 20000, 100, false,
           UnitGroup::ALTITUDE, renderer.clip_altitude);

  AddFloat(_("Margin"),
           _("For auto and all below airspace mode, this is the altitude above/below which airspace is included."),
           "%.0f %s", "%.0f", 0, 10000, 100, false,
           UnitGroup::ALTITUDE, computer.warnings.altitude_warning_margin);

  AddBoolean(_("Warnings"), _("Enable/disable all airspace warnings."),
             computer.enable_warnings, this);

  AddBoolean(_("Warnings dialog"),
             _("Enable/disable displaying airspaces warnings dialog."),
             ui_settings.enable_airspace_warning_dialog, this);
  SetExpertRow(unsigned(ControlIndex::WARNING_DIALOG));

  AddDuration(_("Warning time"),
              _("This is the time before an airspace incursion is estimated at which the system will warn the pilot."),
              seconds{10}, seconds{1000}, seconds{5},
              computer.warnings.warning_time);
  SetExpertRow(unsigned(ControlIndex::WARNING_TIME));

  AddBoolean(_("Repetitive sound"),
             _("Enable/disable repetitive warning sound when airspaces warnings dialog is displayed."),
             computer.warnings.repetitive_sound, this);
  SetExpertRow(unsigned(ControlIndex::REPETITIVE_SOUND));

  AddDuration(_("Acknowledge time"),
              _("This is the time period in which an acknowledged airspace warning will not be repeated."),
              seconds{10}, seconds{1000}, seconds{5},
              computer.warnings.acknowledgement_time);
  SetExpertRow(unsigned(ControlIndex::ACKNOWLEDGE_TIME));

  AddCatalogRow(PageSettingId::AIRSPACE_BLACK_OUTLINE);
  SetExpertRow(unsigned(ControlIndex::BLACK_OUTLINE));

  AddCatalogRow(PageSettingId::AIRSPACE_FILL_MODE);
  SetExpertRow(unsigned(ControlIndex::FILL_MODE));

#if defined(HAVE_HATCHED_BRUSH) && defined(HAVE_ALPHA_BLEND)
  AddBoolean(_("Airspace transparency"), _("If enabled, then airspaces are filled transparently."),
             renderer.transparency);
  SetExpertRow(unsigned(ControlIndex::TRANSPARENCY));
#endif

  SyncCatalogFromForm();
  initial_bundle = bundle;

  ShowDisplayControls(bundle.airspace.altitude_mode);
  ShowWarningControls(computer.enable_warnings);
}


bool
AirspaceConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;

  AirspaceComputerSettings &computer =
    CommonInterface::SetComputerSettings().airspace;
  AirspaceRendererSettings &renderer =
    CommonInterface::SetMapSettings().airspace;
  UISettings &ui_settings = CommonInterface::SetUISettings();

  SyncCatalogFromForm();
  AirspaceDisplaySetting::ApplyLive(bundle);
  changed |= AirspaceDisplaySetting::SaveGlobal(bundle, initial_bundle);

  changed |= SaveValue(unsigned(ControlIndex::CLIP_ALTITUDE),
                       UnitGroup::ALTITUDE, ProfileKeys::ClipAlt,
                       renderer.clip_altitude);

  changed |= SaveValue(unsigned(ControlIndex::ALT_WARNING_MARGIN),
                       UnitGroup::ALTITUDE, ProfileKeys::AltMargin,
                       computer.warnings.altitude_warning_margin);

  changed |= SaveValue(unsigned(ControlIndex::AIRSPACE_WARNINGS),
                       ProfileKeys::AirspaceWarning,
                       computer.enable_warnings);

  changed |= SaveValue(unsigned(ControlIndex::WARNING_DIALOG),
                       ProfileKeys::AirspaceWarningDialog,
                       ui_settings.enable_airspace_warning_dialog);

  if (SaveValue(unsigned(ControlIndex::WARNING_TIME), ProfileKeys::WarningTime,
                computer.warnings.warning_time)) {
    changed = true;
    require_restart = true;
  }

  changed |= SaveValue(unsigned(ControlIndex::REPETITIVE_SOUND),
                       ProfileKeys::RepetitiveSound,
                       computer.warnings.repetitive_sound);

  if (SaveValue(unsigned(ControlIndex::ACKNOWLEDGE_TIME),
                ProfileKeys::AcknowledgementTime,
                computer.warnings.acknowledgement_time)) {
    changed = true;
    require_restart = true;
  }

#if defined(HAVE_HATCHED_BRUSH) && defined(HAVE_ALPHA_BLEND)
  changed |= SaveValue(unsigned(ControlIndex::TRANSPARENCY),
                       ProfileKeys::AirspaceTransparency,
                       renderer.transparency);
#endif

  _changed |= changed;

  return true;
}

std::unique_ptr<Widget>
CreateAirspaceConfigPanel()
{
  return std::make_unique<AirspaceConfigPanel>();
}
