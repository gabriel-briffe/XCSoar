// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "Units/Units.hpp"
#include "Units/Descriptor.hpp"
#include "util/StringFormat.hpp"

#include <algorithm>
#include <cmath>

enum ControlIndex {
  Mode,
  GlideRatio,
  MaxAltitude,
  CellSize,
  IterationCap,
  Contours,
  ContoursMinScale,
  LabelSpacing,
  PanModePath,
};

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

class GlideConeConfigPanel final : public RowFormWidget {
  /** Cached at Prepare for stable edit↔store conversion. */
  double map_scale_to_ruler = 8.;

public:
  GlideConeConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
GlideConeConfigPanel::Prepare(ContainerWindow &parent,
                              const PixelRect &rc) noexcept
{
  const ComputerSettings &settings_computer =
    CommonInterface::GetComputerSettings();
  const GlideConeSettings &glide_cone = settings_computer.glide_cone;

  map_scale_to_ruler = GetMapScaleToRulerFactor();
  if (map_scale_to_ruler <= 0.)
    map_scale_to_ruler = 8.;

  RowFormWidget::Prepare(parent, rc);

  AddEnum(_("Glide cone"),
          _("Terrain-aware glide cone mode.  This is a GPU (OpenGL ES 3.1) "
            "feature and has no effect on devices without compute support."),
          glide_cone_mode_list, unsigned(glide_cone.mode));

  AddFloat(_("Glide ratio"),
           _("Fixed glide ratio (L/D) used for the glide cone computation."),
           "%.0f", "%.0f", 1, 200, 1, false, glide_cone.glide_ratio);

  AddFloat(_("Max. altitude"),
           _("Maximum working altitude [m MSL].  Larger values enlarge the "
             "computed reachable area."),
           "%.0f m", "%.0f", 100, 10000, 50, false, glide_cone.max_altitude);

  AddFloat(_("Cell size"),
           _("Target size of one compute cell [m].  Terrain is resampled by "
             "taking the highest DEM sample in each cell."),
           "%.0f m", "%.0f", 50, 2000, 50, false, glide_cone.cell_size);

  AddInteger(_("Iteration cap"),
             _("Upper bound on the number of GPU propagation iterations."),
             "%d", "%d", 100, 20000, 100, int(glide_cone.iteration_cap));

  AddBoolean(_("Contours"),
             _("Draw 100 m altitude contour lines of the reachable area."),
             glide_cone.contours);

  /* UI is map-ruler distance (scale bar); store is GetMapScale() metres. */
  const unsigned list_max = GetDefaultMaxThresholdUser();
  const double ruler_user = Units::ToUserDistance(
    glide_cone.contours_min_scale * map_scale_to_ruler);
  auto *scale_control = AddEnum(
    _("Contours min scale"),
    _("Only show contours and labels when the map scale bar is at most "
      "this distance (same meaning as topography label thresholds)."));
  auto &scale_df = *(DataFieldEnum *)scale_control->GetDataField();
  FillThresholdChoices(scale_df, list_max,
                       Units::GetUnitName(Units::GetUserDistanceUnit()));
  scale_df.SetValue(SnapThresholdChoice(ruler_user, list_max));
  scale_control->RefreshDisplay();

  AddInteger(_("Label distance"),
             _("Minimum screen distance between labels of the same "
               "altitude, as a percentage of the shorter map side."),
             "%d %%", "%d", 20, 100, 5, int(glide_cone.label_spacing));

  AddBoolean(_("Pan mode path"),
             _("Draw the glide path and GlideCone altitude at the pan "
               "crosshair while panning the map."),
             glide_cone.pan_mode_path);
}

bool
GlideConeConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;

  ComputerSettings &settings_computer = CommonInterface::SetComputerSettings();
  GlideConeSettings &glide_cone = settings_computer.glide_cone;

  changed |= SaveValueEnum(Mode, ProfileKeys::GlideConeMode, glide_cone.mode);

  changed |= SaveValue(GlideRatio, ProfileKeys::GlideConeGlideRatio,
                       glide_cone.glide_ratio);

  changed |= SaveValue(MaxAltitude, ProfileKeys::GlideConeMaxAltitude,
                       glide_cone.max_altitude);

  changed |= SaveValue(CellSize, ProfileKeys::GlideConeCellSize,
                       glide_cone.cell_size);

  if (SaveValueInteger(IterationCap, glide_cone.iteration_cap)) {
    Profile::Set(ProfileKeys::GlideConeIterationCap, glide_cone.iteration_cap);
    changed = true;
  }

  changed |= SaveValue(Contours, ProfileKeys::GlideConeContours,
                       glide_cone.contours);

  const unsigned ruler_user = GetValueEnum(ContoursMinScale);
  const double map_scale_m = std::max(
    1., Units::ToSysDistance(double(ruler_user)) / map_scale_to_ruler);
  if (map_scale_m != glide_cone.contours_min_scale) {
    glide_cone.contours_min_scale = map_scale_m;
    Profile::Set(ProfileKeys::GlideConeContoursMinScale, map_scale_m);
    changed = true;
  }

  if (SaveValueInteger(LabelSpacing, glide_cone.label_spacing)) {
    if (glide_cone.label_spacing < 20)
      glide_cone.label_spacing = 20;
    else if (glide_cone.label_spacing > 100)
      glide_cone.label_spacing = 100;
    Profile::Set(ProfileKeys::GlideConeLabelSpacing, glide_cone.label_spacing);
    changed = true;
  }

  changed |= SaveValue(PanModePath, ProfileKeys::GlideConePanModePath,
                       glide_cone.pan_mode_path);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateGlideConeConfigPanel()
{
  return std::make_unique<GlideConeConfigPanel>();
}
