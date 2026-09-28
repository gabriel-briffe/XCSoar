// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StringFormat.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

static constexpr StaticEnumChoice glide_cone_mode_list[] = {
  { GlideConeSettings::Mode::OFF, N_("Off") },
  { GlideConeSettings::Mode::SINGLE, N_("Single"),
    N_("Compute the glide cone for the current Goto airport.") },
  { GlideConeSettings::Mode::COMBINED, N_("Combined"),
    N_("Compute a combined glide cone from all landables within a moving "
       "window around the aircraft.") },
  nullptr
};

/**
 * Convert #GetMapScale() metres ↔ map-ruler metres (screen width).
 * factor = 8 × width / short_edge (see WindowProjection).
 */
[[gnu::pure]]
static double
GetMapScaleToRulerFactor() noexcept
{
  const GlueMapWindow *map = UIGlobals::GetMap();
  if (map == nullptr)
    return 8.;

  const auto &projection = map->VisibleProjection();
  const unsigned width = projection.GetScreenSize().width;
  const unsigned min_edge = projection.GetMinScreenDistance();
  if (width == 0 || min_edge == 0)
    return 8.;

  return 8. * double(width) / double(min_edge);
}

/** Default maximum for the chooser: 600 km, or 300 for mi/nm. */
[[gnu::pure]]
static unsigned
GetDefaultMaxThresholdUser() noexcept
{
  if (Units::GetUserDistanceUnit() == Unit::KILOMETER)
    return 600;

  return 300;
}

[[gnu::pure]]
static unsigned
NextThresholdChoice(unsigned value) noexcept
{
  if (value < 20) {
    const unsigned fine =
      Units::GetUserDistanceUnit() == Unit::KILOMETER ? 5u : 2u;
    return value + fine;
  }

  return value + 10;
}

static void
FillThresholdChoices(DataFieldEnum &df, unsigned max_user,
                     const char *unit_name) noexcept
{
  char label[32];
  for (unsigned value = 0;;) {
    StringFormat(label, sizeof(label), "%u %s", value, unit_name);
    df.AddChoice(value, label, label);

    if (value >= max_user)
      break;

    const unsigned next = NextThresholdChoice(value);
    if (next <= value)
      break;
    value = next;
  }
}

[[gnu::pure]]
static unsigned
SnapThresholdChoice(double value_user, unsigned max_user) noexcept
{
  if (value_user <= 0.)
    return 0;

  unsigned best = 0;
  double best_delta = value_user;

  for (unsigned value = 0;;) {
    const double delta = std::fabs(double(value) - value_user);
    if (delta < best_delta) {
      best_delta = delta;
      best = value;
    }

    if (value >= max_user)
      break;

    const unsigned next = NextThresholdChoice(value);
    if (next <= value)
      break;
    value = next;
  }

  return best;
}

std::unique_ptr<Widget>
CreateGlideConeConfigPanel()
{
  const GlideConeSettings &glide_cone =
    CommonInterface::GetComputerSettings().glide_cone;

  double map_scale_to_ruler = GetMapScaleToRulerFactor();
  if (map_scale_to_ruler <= 0.)
    map_scale_to_ruler = 8.;

  const unsigned list_max = GetDefaultMaxThresholdUser();
  const double ruler_user = Units::ToUserDistance(
    glide_cone.contours_min_scale * map_scale_to_ruler);

  struct Fields {
    GlideConeSettings::Mode mode;
    double glide_ratio;
    double max_altitude;
    double cell_size;
    int iteration_cap;
    bool contours;
    unsigned contours_min_scale_user;
    int label_spacing;
    bool pan_mode_path;
    double map_scale_to_ruler;
  };

  auto fields = std::make_shared<Fields>(Fields{
    glide_cone.mode,
    glide_cone.glide_ratio,
    glide_cone.max_altitude,
    glide_cone.cell_size,
    int(glide_cone.iteration_cap),
    glide_cone.contours,
    SnapThresholdChoice(ruler_user, list_max),
    int(glide_cone.label_spacing),
    glide_cone.pan_mode_path,
    map_scale_to_ruler,
  });

  const auto mode_shown = [fields] {
    return fields->mode != GlideConeSettings::Mode::OFF;
  };
  const auto contours_shown = [fields] {
    return fields->mode != GlideConeSettings::Mode::OFF && fields->contours;
  };

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  auto *page = list.get();
  list->AddGroup(nullptr);

  list->AddEnum(_("Glide cone"),
                _("Terrain-aware glide cone mode.  This is a GPU (OpenGL ES 3.1) "
                  "feature and has no effect on devices without compute support."),
                glide_cone_mode_list, fields->mode);

  list->AddFloat(_("Glide ratio"),
                 _("Fixed glide ratio (L/D) used for the glide cone computation."),
                 "%.0f", "%.0f", 1, 200, 1, false, fields->glide_ratio,
                 false, mode_shown);

  list->AddFloat(_("Max. altitude"),
                 _("Maximum working altitude [m MSL].  Larger values enlarge the "
                   "computed reachable area."),
                 "%.0f m", "%.0f", 100, 10000, 50, false, fields->max_altitude,
                 false, mode_shown);

  list->AddFloat(_("Cell size"),
                 _("Target size of one compute cell [m].  Terrain is resampled by "
                   "taking the highest DEM sample in each cell."),
                 "%.0f m", "%.0f", 50, 2000, 50, false, fields->cell_size,
                 false, mode_shown);

  list->AddInteger(_("Iteration cap"),
                   _("Upper bound on the number of GPU propagation iterations."),
                   "%d", "%d", 100, 20000, 100, fields->iteration_cap,
                   false, mode_shown);

  list->AddSwitch(_("Contours"),
                  _("Draw 100 m altitude contour lines of the reachable area."),
                  fields->contours, false, mode_shown);

  const char *const scale_help =
    _("Only show contours and labels when the map scale bar is at most "
      "this distance (same meaning as topography label thresholds).");
  const char *unit_name =
    Units::GetUnitName(Units::GetUserDistanceUnit());

  list->AddValue(_("Contours min scale"), scale_help,
                 [fields, list_max, unit_name, contours_shown](
                   GroupedListWidget::ValueState &state) {
                   state.hidden = !contours_shown();
                   DataFieldEnum df;
                   FillThresholdChoices(df, list_max, unit_name);
                   df.SetValue(fields->contours_min_scale_user);
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page, list_max, unit_name, scale_help] {
                   DataFieldEnum df;
                   FillThresholdChoices(df, list_max, unit_name);
                   df.SetValue(fields->contours_min_scale_user);
                   if (!ComboPicker(_("Contours min scale"), df, scale_help) ||
                       df.GetValue() == fields->contours_min_scale_user)
                     return;
                   fields->contours_min_scale_user = df.GetValue();
                   page->UpdateValues();
                 });

  list->AddInteger(_("Label distance"),
                   _("Minimum screen distance between labels of the same "
                     "altitude, as a percentage of the shorter map side."),
                   "%d %%", "%d", 20, 100, 5, fields->label_spacing,
                   false, contours_shown);

  list->AddSwitch(_("Pan mode path"),
                  _("Draw the glide path and GlideCone altitude at the pan "
                    "crosshair while panning the map."),
                  fields->pan_mode_path, false, mode_shown);

  list->SetSaveCallback([fields](bool &changed) {
    GlideConeSettings &glide_cone =
      CommonInterface::SetComputerSettings().glide_cone;

    ConfigPanel::CommitSetting(changed, glide_cone.mode, fields->mode,
                               ProfileKeys::GlideConeMode);
    ConfigPanel::CommitSetting(changed, glide_cone.glide_ratio,
                               fields->glide_ratio,
                               ProfileKeys::GlideConeGlideRatio);
    ConfigPanel::CommitSetting(changed, glide_cone.max_altitude,
                               fields->max_altitude,
                               ProfileKeys::GlideConeMaxAltitude);
    ConfigPanel::CommitSetting(changed, glide_cone.cell_size,
                               fields->cell_size,
                               ProfileKeys::GlideConeCellSize);

    unsigned iteration_cap = unsigned(fields->iteration_cap);
    if (ConfigPanel::CommitSetting(changed, glide_cone.iteration_cap,
                                   iteration_cap))
      Profile::Set(ProfileKeys::GlideConeIterationCap,
                   glide_cone.iteration_cap);

    ConfigPanel::CommitSetting(changed, glide_cone.contours,
                               fields->contours,
                               ProfileKeys::GlideConeContours);

    const double map_scale_m = std::max(
      1.,
      Units::ToSysDistance(double(fields->contours_min_scale_user)) /
        fields->map_scale_to_ruler);
    if (map_scale_m != glide_cone.contours_min_scale) {
      glide_cone.contours_min_scale = map_scale_m;
      Profile::Set(ProfileKeys::GlideConeContoursMinScale, map_scale_m);
      changed = true;
    }

    unsigned label_spacing = unsigned(fields->label_spacing);
    if (label_spacing < 20)
      label_spacing = 20;
    else if (label_spacing > 100)
      label_spacing = 100;
    if (ConfigPanel::CommitSetting(changed, glide_cone.label_spacing,
                                   label_spacing))
      Profile::Set(ProfileKeys::GlideConeLabelSpacing,
                   glide_cone.label_spacing);

    ConfigPanel::CommitSetting(changed, glide_cone.pan_mode_path,
                               fields->pan_mode_path,
                               ProfileKeys::GlideConePanModePath);
    return true;
  });

  return list;
}
