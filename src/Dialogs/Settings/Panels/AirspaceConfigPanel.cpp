// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "AirspaceConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/Airspace/Airspace.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Time.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Math/Util.hpp"
#include "Profile/Keys.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "UtilsSettings.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StaticString.hxx"

#include <cmath>
#include <functional>
#include <memory>

using namespace std::chrono;

static void
AddLinkedSwitch(GroupedListWidget &list, const char *caption,
                const char *help, bool &field) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.toggle = true;
  options.checked = field;
  options.help = help;
  list.AddItem(caption, [&list, &field] {
    field = !field;
    if (list.UpdateValues())
      list.UpdateLayout();
  }, options);
}
static void
AddSeconds(GroupedListWidget &list, const char *caption,
           const char *help, unsigned &value, bool expert,
           std::function<bool()> shown) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.expert = expert;
  options.value_callback =
    [&value, shown = std::move(shown)](GroupedListWidget::ValueState &state) {
      DataFieldTime df(seconds{10}, seconds{1000}, seconds{value},
                       seconds{5}, nullptr);
      state.text = df.GetAsDisplayString();
      if (shown)
        state.hidden = !shown();
    };
  list.AddValue(caption, [&list, caption, help, &value] {
    DataFieldTime df(seconds{10}, seconds{1000}, seconds{value},
                     seconds{5}, nullptr);
    if (!EditDataFieldDialog(caption, df, help))
      return;
    value = static_cast<unsigned>(df.GetValue().count());
    list.UpdateValues();
  }, std::move(options));
}
static constexpr StaticEnumChoice as_display_list[] = {
  { AirspaceDisplayMode::ALLON, N_("All on"),
    N_("All airspaces are displayed.") },
  { AirspaceDisplayMode::CLIP, N_("Clip"),
    N_("Display airspaces below the clip altitude.") },
  { AirspaceDisplayMode::AUTO, NC_("Setting", "Auto"),
    N_("Display airspaces within a margin of the glider.") },
  { AirspaceDisplayMode::ALLBELOW, N_("All below"),
    N_("Display airspaces below the glider or within a margin.") },
  nullptr
};

static constexpr StaticEnumChoice as_fill_mode_list[] = {
  { AirspaceRendererSettings::FillMode::DEFAULT, N_("Default"),
    N_("This selects the best performing option for your hardware. "
      "In fact it favours 'fill padding' except for PPC 2000 system.") },
  { AirspaceRendererSettings::FillMode::ALL, N_("Fill all"),
    N_("Transparently fills the airspace colour over the whole area.") },
  { AirspaceRendererSettings::FillMode::PADDING, N_("Fill padding"),
    N_("Draws a solid outline with a half transparent border around the airspace.") },
  { AirspaceRendererSettings::FillMode::NONE, N_("No fill"),
    N_("Don't fill the airspace area.") },
  nullptr
};

static constexpr StaticEnumChoice as_label_selection_list[] = {
  { AirspaceRendererSettings::LabelSelection::NONE, N_("None"),
    N_("No labels will be displayed.") },
  { AirspaceRendererSettings::LabelSelection::ALL, N_("All"),
    N_("All labels will be displayed.") },
  nullptr
};

std::unique_ptr<Widget>
CreateAirspaceConfigPanel()
{
  const AirspaceComputerSettings &computer =
    CommonInterface::GetComputerSettings().airspace;
  const AirspaceRendererSettings &renderer =
    CommonInterface::GetMapSettings().airspace;
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  struct Fields {
    AirspaceDisplayMode altitude_mode;
    AirspaceRendererSettings::LabelSelection label_selection;
#ifdef HAVE_HTTP
    bool show_notam_labels;
#endif
    double clip_altitude;
    double margin;
    bool warnings;
    bool warning_dialog;
    unsigned warning_time;
    bool repetitive_sound;
    unsigned acknowledge_time;
    bool black_outline;
    AirspaceRendererSettings::FillMode fill_mode;
    StaticString<24> altitude_format;
  };

  const Unit altitude_unit = Units::GetUserUnitByGroup(UnitGroup::ALTITUDE);

  auto fields = std::make_shared<Fields>(Fields{
    renderer.altitude_mode,
    renderer.label_selection,
#ifdef HAVE_HTTP
    renderer.show_notam_labels,
#endif
    Units::ToUserUnit(renderer.clip_altitude, altitude_unit),
    Units::ToUserUnit(computer.warnings.altitude_warning_margin,
                      altitude_unit),
    computer.enable_warnings,
    ui_settings.enable_airspace_warning_dialog,
    computer.warnings.warning_time.count(),
    computer.warnings.repetitive_sound,
    computer.warnings.acknowledgement_time.count(),
    renderer.black_outline,
    renderer.fill_mode,
    {},
  });
  fields->altitude_format.Format("%%.0f %s",
                                 Units::GetUnitName(altitude_unit));

  const auto warnings_shown = [fields] { return fields->warnings; };
  const char *const altitude_format = fields->altitude_format.c_str();

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddEnum(_("Airspace display"),
                _("Controls filtering of airspace for display and warnings. The airspace filter button also allows filtering of display and warnings independently for each airspace class."),
                as_display_list, fields->altitude_mode);
  list->AddEnum(_("Label visibility"),
                _("Determines what labels are displayed."),
                as_label_selection_list, fields->label_selection, true);

#ifdef HAVE_HTTP
  list->AddSwitch(_("Show NOTAM labels"),
                  _("Show brief NOTAM text labels on the map when zoomed in sufficiently."),
                  fields->show_notam_labels, true);
#endif

  list->AddFloat(_("Clip altitude"),
                 _("For clip airspace mode, this is the altitude below which airspace is displayed."),
                 altitude_format, "%.0f", 0, 20000, 100, false,
                 fields->clip_altitude, false,
                 [fields] {
                   return fields->altitude_mode == AirspaceDisplayMode::CLIP;
                 });
  list->AddFloat(_("Margin"),
                 _("For auto and all below airspace mode, this is the altitude above/below which airspace is included."),
                 altitude_format, "%.0f", 0, 10000, 100, false,
                 fields->margin, false,
                 [fields] {
                   return fields->altitude_mode == AirspaceDisplayMode::AUTO
                     || fields->altitude_mode ==
                        AirspaceDisplayMode::ALLBELOW;
                 });
  AddLinkedSwitch(*list, _("Warnings"),
                  _("Enable/disable all airspace warnings."),
                  fields->warnings);
  list->AddSwitch(_("Warnings dialog"),
                  _("Enable/disable displaying airspaces warnings dialog."),
                  fields->warning_dialog, true, warnings_shown);
  AddSeconds(*list, _("Warning time"),
             _("This is the time before an airspace incursion is estimated at which the system will warn the pilot."),
             fields->warning_time, true, warnings_shown);
  list->AddSwitch(_("Repetitive sound"),
                  _("Enable/disable repetitive warning sound when airspaces warnings dialog is displayed."),
                  fields->repetitive_sound, true, warnings_shown);
  AddSeconds(*list, _("Acknowledge time"),
             _("This is the time period in which an acknowledged airspace warning will not be repeated."),
             fields->acknowledge_time, true, warnings_shown);
  list->AddSwitch(_("Use black outline"),
                  _("Draw a black outline around each airspace rather than the airspace color."),
                  fields->black_outline, true);
  list->AddEnum(_("Airspace fill mode"),
                _("Specifies the mode for filling the airspace area."),
                as_fill_mode_list, fields->fill_mode, true);

  list->SetVisibilityCallback([](bool visible) {
    if (visible) {
      ConfigPanel::BorrowExtraButton(1, _("Colours"),
                                     [] { dlgAirspaceShowModal(true); });
      ConfigPanel::BorrowExtraButton(2, _("Filter"),
                                     [] { dlgAirspaceShowModal(false); });
    } else {
      ConfigPanel::ReturnExtraButton(1);
      ConfigPanel::ReturnExtraButton(2);
    }
  });
  list->SetSaveCallback([fields](bool &changed) {
    AirspaceComputerSettings &computer =
      CommonInterface::SetComputerSettings().airspace;
    AirspaceRendererSettings &renderer =
      CommonInterface::SetMapSettings().airspace;
    UISettings &ui_settings = CommonInterface::SetUISettings();
    const Unit unit = Units::GetUserUnitByGroup(UnitGroup::ALTITUDE);

    changed |= ConfigPanel::CommitSetting(
      changed, renderer.altitude_mode, fields->altitude_mode,
      ProfileKeys::AltMode);
    changed |= ConfigPanel::CommitSetting(
      changed, renderer.label_selection, fields->label_selection,
      ProfileKeys::AirspaceLabelSelection);
#ifdef HAVE_HTTP
    changed |= ConfigPanel::CommitSetting(
      changed, renderer.show_notam_labels, fields->show_notam_labels,
      ProfileKeys::AirspaceShowNOTAMLabels);
#endif
    if (std::fabs(fields->clip_altitude -
                  Units::ToUserUnit(renderer.clip_altitude, unit)) >= 1.)
      ConfigPanel::CommitSetting(
        changed, renderer.clip_altitude,
        static_cast<unsigned>(iround(Units::ToSysUnit(
          fields->clip_altitude, unit))),
        ProfileKeys::ClipAlt);
    if (std::fabs(fields->margin -
                  Units::ToUserUnit(
                    computer.warnings.altitude_warning_margin, unit)) >= 1.)
      ConfigPanel::CommitSetting(
        changed, computer.warnings.altitude_warning_margin,
        static_cast<unsigned>(iround(Units::ToSysUnit(fields->margin, unit))),
        ProfileKeys::AltMargin);
    changed |= ConfigPanel::CommitSetting(
      changed, computer.enable_warnings, fields->warnings,
      ProfileKeys::AirspaceWarning);
    changed |= ConfigPanel::CommitSetting(
      changed, ui_settings.enable_airspace_warning_dialog,
      fields->warning_dialog, ProfileKeys::AirspaceWarningDialog);
    if (ConfigPanel::CommitSetting(
          changed, computer.warnings.warning_time,
          AirspaceWarningConfig::Duration{fields->warning_time},
          ProfileKeys::WarningTime))
      require_restart = true;
    changed |= ConfigPanel::CommitSetting(
      changed, computer.warnings.repetitive_sound, fields->repetitive_sound,
      ProfileKeys::RepetitiveSound);
    if (ConfigPanel::CommitSetting(
          changed, computer.warnings.acknowledgement_time,
          AirspaceWarningConfig::Duration{fields->acknowledge_time},
          ProfileKeys::AcknowledgementTime))
      require_restart = true;
    changed |= ConfigPanel::CommitSetting(
      changed, renderer.black_outline, fields->black_outline,
      ProfileKeys::AirspaceBlackOutline);
    changed |= ConfigPanel::CommitSetting(
      changed, renderer.fill_mode, fields->fill_mode,
      ProfileKeys::AirspaceFillMode);
    return true;
  });

  return list;
}
