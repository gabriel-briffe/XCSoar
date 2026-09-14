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

enum ControlIndex {
  Mode,
  GlideRatio,
  MaxAltitude,
  IterationCap,
  Contours,
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

  AddInteger(_("Iteration cap"),
             _("Upper bound on the number of GPU propagation iterations."),
             "%d", "%d", 100, 20000, 100, int(glide_cone.iteration_cap));

  AddBoolean(_("Contours"),
             _("Draw 100 m altitude contour lines of the reachable area."),
             glide_cone.contours);
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

  if (SaveValueInteger(IterationCap, glide_cone.iteration_cap)) {
    Profile::Set(ProfileKeys::GlideConeIterationCap, glide_cone.iteration_cap);
    changed = true;
  }

  changed |= SaveValue(Contours, ProfileKeys::GlideConeContours,
                       glide_cone.contours);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateGlideConeConfigPanel()
{
  return std::make_unique<GlideConeConfigPanel>();
}
