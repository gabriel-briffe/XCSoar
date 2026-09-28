// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeConfigPanel.hpp"
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "UIGlobals.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "util/StaticString.hxx"

#include <algorithm>
#include <cmath>
#include <vector>

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

/**
 * The terrain-aware glide cone: the ratio, the grid, and when the
 * contours are drawn.
 */
class GlideConeConfigPanel final : public ConfigListPanel {
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

  void AddFixedItem(const char *caption, const char *help,
                    const char *format, int min_value, int max_value,
                    int step, double &value) noexcept;

  void PickContoursScale() noexcept;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
GlideConeConfigPanel::AddFixedItem(const char *caption, const char *help,
                                   const char *format,
                                   int min_value, int max_value, int step,
                                   double &value) noexcept
{
  const int shown = int(std::lround(value));
  StaticString<16> text;
  text.Format(format, shown);

  AddItem(caption, [this, caption, help, format, min_value, max_value,
                    step, &value](){
    int picked = int(std::lround(value));
    if (!PickNumber(caption, help, min_value, max_value, step, picked,
                    [format](StaticString<32> &s, int v){
                      s.Format(format, v);
                    }))
      return;

    value = picked;
    Refresh();
  }, {.value = text.c_str(), .chevron = true, .help = help});
}

void
GlideConeConfigPanel::PickContoursScale() noexcept
{
  const char *const help =
    _("Only show contours and labels when the map scale bar is at most "
      "this distance (same meaning as topography label thresholds).");
  const char *unit_name =
    Units::GetUnitName(Units::GetUserDistanceUnit());
  const unsigned max_user = GetDefaultMaxThresholdUser();

  struct Choice {
    unsigned value;
    StaticString<32> label;
  };

  std::vector<Choice> items;
  for (unsigned value = 0;;) {
    Choice choice;
    choice.value = value;
    choice.label.Format("%u %s", value, unit_name);
    items.push_back(std::move(choice));

    if (value >= max_user)
      break;

    const unsigned next = NextThresholdChoice(value);
    if (next <= value)
      break;
    value = next;
  }

  const unsigned snapped =
    SnapThresholdChoice(contours_min_scale_user, max_user);

  std::vector<PickerChoice> choices;
  choices.reserve(items.size());
  int current = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].value == snapped)
      current = int(i);
    choices.push_back({items[i].label.c_str()});
  }

  const int picked = PickChoice(_("Contours min scale"), help,
                                choices, current);
  if (picked < 0 || items[picked].value == contours_min_scale_user)
    return;

  contours_min_scale_user = items[picked].value;
  Refresh();
}

void
GlideConeConfigPanel::LoadSettings() noexcept
{
  const GlideConeSettings &glide_cone =
    CommonInterface::GetComputerSettings().glide_cone;

  map_scale_to_ruler = GetMapScaleToRulerFactor();
  if (map_scale_to_ruler <= 0.)
    map_scale_to_ruler = 8.;

  const unsigned list_max = GetDefaultMaxThresholdUser();
  const double ruler_user = Units::ToUserDistance(
    glide_cone.contours_min_scale * map_scale_to_ruler);

  mode = glide_cone.mode;
  glide_ratio = glide_cone.glide_ratio;
  max_altitude = glide_cone.max_altitude;
  cell_size = glide_cone.cell_size;
  iteration_cap = int(glide_cone.iteration_cap);
  contours = glide_cone.contours;
  contours_min_scale_user = SnapThresholdChoice(ruler_user, list_max);
  label_spacing = int(glide_cone.label_spacing);
  pan_mode_path = glide_cone.pan_mode_path;
}

void
GlideConeConfigPanel::Fill() noexcept
{
  AddGroup();

  AddEnumItem(_("Glide cone"),
              _("Terrain-aware glide cone mode.  This is a GPU "
                "(OpenGL ES 3.1) feature and has no effect on devices "
                "without compute support."),
              glide_cone_mode_list, mode);

  if (mode == GlideConeSettings::Mode::OFF)
    return;

  AddFixedItem(_("Glide ratio"),
               _("Fixed glide ratio (L/D) used for the glide cone "
                 "computation."),
               "%d", 1, 200, 1, glide_ratio);

  AddFixedItem(_("Max. altitude"),
               _("Maximum working altitude [m MSL].  Larger values enlarge "
                 "the computed reachable area."),
               "%d m", 100, 10000, 50, max_altitude);

  AddFixedItem(_("Cell size"),
               _("Target size of one compute cell [m].  Terrain is "
                 "resampled by taking the highest DEM sample in each cell."),
               "%d m", 50, 2000, 50, cell_size);

  StaticString<16> iterations;
  iterations.Format("%d", iteration_cap);
  AddItem(_("Iteration cap"), [this](){
    if (PickNumber(_("Iteration cap"),
                   _("Upper bound on the number of GPU propagation "
                     "iterations."),
                   100, 20000, 100, iteration_cap,
                   [](StaticString<32> &s, int v){ s.Format("%d", v); }))
      Refresh();
  }, {.value = iterations.c_str(), .chevron = true,
      .help = _("Upper bound on the number of GPU propagation "
                "iterations.")});

  AddToggleItem(_("Contours"),
                _("Draw 100 m altitude contour lines of the reachable area."),
                contours);

  if (contours) {
    const char *unit_name =
      Units::GetUnitName(Units::GetUserDistanceUnit());
    StaticString<32> scale;
    scale.Format("%u %s", contours_min_scale_user, unit_name);
    AddItem(_("Contours min scale"), [this](){ PickContoursScale(); },
            {.value = scale.c_str(), .chevron = true,
             .help = _("Only show contours and labels when the map scale "
                       "bar is at most this distance (same meaning as "
                       "topography label thresholds).")});

    AddPercentItem(_("Label distance"),
                   _("Minimum screen distance between labels of the same "
                     "altitude, as a percentage of the shorter map side."),
                   20, 100, 5, label_spacing);
  }

  AddToggleItem(_("Pan mode path"),
                _("Draw the glide path and GlideCone altitude at the pan "
                  "crosshair while panning the map."),
                pan_mode_path);
}

bool
GlideConeConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  GlideConeSettings &glide_cone =
    CommonInterface::SetComputerSettings().glide_cone;

  changed |= Profile::Update(ProfileKeys::GlideConeMode,
                             glide_cone.mode, mode);
  changed |= Profile::Update(ProfileKeys::GlideConeGlideRatio,
                             glide_cone.glide_ratio, glide_ratio);
  changed |= Profile::Update(ProfileKeys::GlideConeMaxAltitude,
                             glide_cone.max_altitude, max_altitude);
  changed |= Profile::Update(ProfileKeys::GlideConeCellSize,
                             glide_cone.cell_size, cell_size);

  const unsigned iterations = unsigned(iteration_cap);
  changed |= Profile::Update(ProfileKeys::GlideConeIterationCap,
                             glide_cone.iteration_cap, iterations);

  changed |= Profile::Update(ProfileKeys::GlideConeContours,
                             glide_cone.contours, contours);

  const double map_scale_m = std::max(
    1.,
    Units::ToSysDistance(double(contours_min_scale_user)) /
      map_scale_to_ruler);
  if (map_scale_m != glide_cone.contours_min_scale) {
    glide_cone.contours_min_scale = map_scale_m;
    Profile::Set(ProfileKeys::GlideConeContoursMinScale, map_scale_m);
    changed = true;
  }

  unsigned spacing = unsigned(label_spacing);
  if (spacing < 20)
    spacing = 20;
  else if (spacing > 100)
    spacing = 100;
  changed |= Profile::Update(ProfileKeys::GlideConeLabelSpacing,
                             glide_cone.label_spacing, spacing);

  changed |= Profile::Update(ProfileKeys::GlideConePanModePath,
                             glide_cone.pan_mode_path, pan_mode_path);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateGlideConeConfigPanel()
{
  return std::make_unique<GlideConeConfigPanel>();
}
