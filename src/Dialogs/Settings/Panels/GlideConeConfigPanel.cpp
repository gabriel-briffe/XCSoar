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
#include "LogFile.hpp"

enum ControlIndex {
  Mode,
  GlideRatio,
  MaxAltitude,
  CellSize,
  IterationCap,
  Contours,
  ContoursMinScale,
  LabelSpacing,
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

class GlideConeConfigPanel final : public RowFormWidget {
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

  AddFloat(_("Contours min scale"),
           _("Only show contours and labels when the map scale [m] is at "
             "most this value, i.e. when zoomed in far enough."),
           "%.0f m", "%.0f", 1000, 500000, 1000, false,
           glide_cone.contours_min_scale);

  AddInteger(_("Label distance"),
             _("Minimum screen distance between labels of the same "
               "altitude, as a percentage of the shorter map side."),
             "%d %%", "%d", 20, 100, 5, int(glide_cone.label_spacing));
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

  changed |= SaveValue(ContoursMinScale, ProfileKeys::GlideConeContoursMinScale,
                       glide_cone.contours_min_scale);

  if (SaveValueInteger(LabelSpacing, glide_cone.label_spacing)) {
    if (glide_cone.label_spacing < 20)
      glide_cone.label_spacing = 20;
    else if (glide_cone.label_spacing > 100)
      glide_cone.label_spacing = 100;
    Profile::Set(ProfileKeys::GlideConeLabelSpacing, glide_cone.label_spacing);
    changed = true;
  }

  if (changed)
    LogFmt("glidecones: config saved mode={} L/D={:.0f} max_alt={:.0f} "
           "cell={:.0f}",
           int(glide_cone.mode), glide_cone.glide_ratio,
           glide_cone.max_altitude, glide_cone.cell_size);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateGlideConeConfigPanel()
{
  return std::make_unique<GlideConeConfigPanel>();
}
