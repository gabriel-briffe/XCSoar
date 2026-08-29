// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TopographyDisplayConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Form/DataField/Listener.hpp"
#include "Form/DataField/Enum.hpp"
#include "Topography/TopographyStore.hpp"
#include "Topography/TopographyFile.hpp"
#include "Topography/TopographySettings.hpp"
#include "Components.hpp"
#include "DataComponents.hpp"
#include "ActionInterface.hpp"
#include "Message.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/Topography/TopographyDialogs.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "Projection/MapWindowProjection.hpp"
#include "Units/Units.hpp"
#include "Units/Descriptor.hpp"
#include "util/StringCompare.hxx"
#include "util/StringFormat.hpp"
#include "util/StaticString.hxx"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

/** Enum value 0 = all layers; 1..n = layers[n-1]. */
constexpr unsigned ALL_LAYERS = 0;

/**
 * Last layer chosen on the Topology settings page (session only).
 */
bool last_layer_all = true;
StaticString<64> last_layer_name;

enum ControlIndex {
  Layer,
  ShapeThreshold,
  LabelThreshold,
  ImportantThreshold,
  ResetLayer,
  ResetAll,
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

/**
 * Default maximum for the "All layers" chooser: 600 km, or half
 * (300) for statute / nautical miles.
 */
[[gnu::pure]]
static unsigned
GetDefaultMaxThresholdUser() noexcept
{
  if (Units::GetUserDistanceUnit() == Unit::KILOMETER)
    return 600;

  return 300;
}

/**
 * Discrete threshold choices in user distance units:
 * km: 0, 5, 10, 15, 20, 30, 40, … (step 10 above 20)
 * mi/nm: 0, 2, 4, …, 20, 30, 40, … (step 10 above 20)
 */
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

} // namespace

class TopographyDisplayConfigPanel final
  : public RowFormWidget, DataFieldListener {

  TopographyStore *store = nullptr;
  std::vector<TopographyFile *> layers;
  unsigned selected_enum = ALL_LAYERS;

  /** Cached at Prepare/Show for stable edit↔store conversion. */
  double map_scale_to_ruler = 8.;

  /**
   * Global ceilings from the "All layers" menu (user distance).
   * Individual layer choosers cannot exceed these.
   */
  unsigned all_ceiling_shape = 0;
  unsigned all_ceiling_label = 0;
  unsigned all_ceiling_important = 0;

  [[gnu::pure]]
  bool IsAllLayers() const noexcept {
    return selected_enum == ALL_LAYERS;
  }

  [[gnu::pure]]
  bool AnyLayerHasLabels() const noexcept;

  void RefreshRulerFactor() noexcept;
  double ToRulerUser(double map_scale_m) const noexcept;
  double FromRulerUser(double ruler_user) const noexcept;

  void RebuildThresholdField(unsigned control, unsigned max_user,
                             unsigned value) noexcept;
  void RebuildThresholdEnumsForMode() noexcept;
  void CaptureAllLayerCeilings() noexcept;
  void RememberSelectedLayer() noexcept;
  void RestoreSelectedLayer(DataFieldEnum &layer_field) noexcept;

  void LoadThresholdEnum(unsigned control, double map_scale_m,
                         unsigned max_user) noexcept;
  unsigned GetThresholdUser(unsigned control) const noexcept;

  void ApplySelectedLayer() noexcept;
  void LoadSelectedLayer() noexcept;
  void SyncLabelThresholdLimits() noexcept;
  void UpdateLabelRowVisibility() noexcept;

  void SetLayerThresholds(TopographyFile &file,
                          double shape_m, double label_m,
                          double important_m) noexcept;

public:
  TopographyDisplayConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  void Show(const PixelRect &rc) noexcept override;
  void Hide() noexcept override;
  bool Save(bool &changed) noexcept override;

protected:
  void OnModified(DataField &df) noexcept override;
};

bool
TopographyDisplayConfigPanel::AnyLayerHasLabels() const noexcept
{
  for (const TopographyFile *file : layers)
    if (file->HasLabels())
      return true;
  return false;
}

void
TopographyDisplayConfigPanel::RefreshRulerFactor() noexcept
{
  map_scale_to_ruler = GetMapScaleToRulerFactor();
  if (map_scale_to_ruler <= 0.)
    map_scale_to_ruler = 8.;
}

double
TopographyDisplayConfigPanel::ToRulerUser(double map_scale_m) const noexcept
{
  return Units::ToUserDistance(map_scale_m * map_scale_to_ruler);
}

double
TopographyDisplayConfigPanel::FromRulerUser(double ruler_user) const noexcept
{
  return Units::ToSysDistance(ruler_user) / map_scale_to_ruler;
}

void
TopographyDisplayConfigPanel::RebuildThresholdField(unsigned control,
                                                    unsigned max_user,
                                                    unsigned value) noexcept
{
  const char *unit_name =
    Units::GetUnitName(Units::GetUserDistanceUnit());
  auto &df = (DataFieldEnum &)GetDataField(control);
  df.ClearChoices();
  FillThresholdChoices(df, max_user, unit_name);
  df.SetValue(SnapThresholdChoice(value, max_user));
  GetControl(control).RefreshDisplay();
}

void
TopographyDisplayConfigPanel::RebuildThresholdEnumsForMode() noexcept
{
  if (IsAllLayers()) {
    const unsigned list_max = GetDefaultMaxThresholdUser();
    RebuildThresholdField(ShapeThreshold, list_max, all_ceiling_shape);
    RebuildThresholdField(LabelThreshold, list_max, all_ceiling_label);
    RebuildThresholdField(ImportantThreshold, list_max,
                          all_ceiling_important);
    return;
  }

  /* Individual layers: chooser max is the All layers ceiling. */
  RebuildThresholdField(ShapeThreshold, all_ceiling_shape,
                        GetThresholdUser(ShapeThreshold));
  RebuildThresholdField(LabelThreshold, all_ceiling_label,
                        GetThresholdUser(LabelThreshold));
  RebuildThresholdField(ImportantThreshold, all_ceiling_important,
                        GetThresholdUser(ImportantThreshold));
}

void
TopographyDisplayConfigPanel::CaptureAllLayerCeilings() noexcept
{
  all_ceiling_shape = GetThresholdUser(ShapeThreshold);
  all_ceiling_label = GetThresholdUser(LabelThreshold);
  all_ceiling_important = GetThresholdUser(ImportantThreshold);

  if (all_ceiling_important > all_ceiling_label)
    all_ceiling_important = all_ceiling_label;
}

void
TopographyDisplayConfigPanel::RememberSelectedLayer() noexcept
{
  if (IsAllLayers()) {
    last_layer_all = true;
    last_layer_name.clear();
    return;
  }

  last_layer_all = false;
  last_layer_name = layers[selected_enum - 1]->GetLayerName();
}

void
TopographyDisplayConfigPanel::RestoreSelectedLayer(DataFieldEnum &layer_field) noexcept
{
  selected_enum = ALL_LAYERS;

  if (!last_layer_all && !last_layer_name.empty()) {
    for (unsigned i = 0; i < layers.size(); ++i) {
      if (StringIsEqual(layers[i]->GetLayerName(), last_layer_name.c_str())) {
        selected_enum = i + 1;
        break;
      }
    }
  }

  layer_field.SetValue(selected_enum);
  GetControl(Layer).RefreshDisplay();
}

void
TopographyDisplayConfigPanel::LoadThresholdEnum(unsigned control,
                                                double map_scale_m,
                                                unsigned max_user) noexcept
{
  LoadValueEnum(control,
                SnapThresholdChoice(ToRulerUser(map_scale_m), max_user));
}

unsigned
TopographyDisplayConfigPanel::GetThresholdUser(unsigned control) const noexcept
{
  return GetValueEnum(control);
}

void
TopographyDisplayConfigPanel::SetLayerThresholds(TopographyFile &file,
                                                 double shape_m,
                                                 double label_m,
                                                 double important_m) noexcept
{
  if (!file.HasLabels()) {
    file.SetThresholds(shape_m,
                       file.GetLabelThreshold(),
                       file.GetImportantLabelThreshold());
    return;
  }

  file.SetThresholds(shape_m, label_m,
                     std::min(important_m, label_m));
}

void
TopographyDisplayConfigPanel::ApplySelectedLayer() noexcept
{
  if (layers.empty())
    return;

  const double shape_m = FromRulerUser(GetThresholdUser(ShapeThreshold));
  const double label_m = FromRulerUser(GetThresholdUser(LabelThreshold));
  const double important_m =
    FromRulerUser(GetThresholdUser(ImportantThreshold));

  if (IsAllLayers()) {
    /* Ceiling only: never raise a layer that already disappears
       earlier (e.g. a road at 50 km stays at 50 when the max is
       set to 600 km). */
    for (TopographyFile *file : layers)
      SetLayerThresholds(*file,
                         std::min(file->GetScaleThreshold(), shape_m),
                         std::min(file->GetLabelThreshold(), label_m),
                         std::min(file->GetImportantLabelThreshold(),
                                  important_m));
    return;
  }

  SetLayerThresholds(*layers[selected_enum - 1],
                     shape_m, label_m, important_m);
}

void
TopographyDisplayConfigPanel::SyncLabelThresholdLimits() noexcept
{
  if (!AnyLayerHasLabels() && IsAllLayers())
    return;
  if (!IsAllLayers() &&
      (selected_enum < 1 || selected_enum > layers.size() ||
       !layers[selected_enum - 1]->HasLabels()))
    return;

  /* Important labels cannot exceed the label threshold; labels may
     exceed the shape threshold (labels without shapes). */
  const unsigned label = GetThresholdUser(LabelThreshold);
  if (GetThresholdUser(ImportantThreshold) > label)
    LoadValueEnum(ImportantThreshold, label);

  GetControl(ImportantThreshold).RefreshDisplay();
}

void
TopographyDisplayConfigPanel::UpdateLabelRowVisibility() noexcept
{
  const bool show = IsAllLayers()
    ? AnyLayerHasLabels()
    : (selected_enum >= 1 && selected_enum <= layers.size() &&
       layers[selected_enum - 1]->HasLabels());

  SetRowVisible(LabelThreshold, show);
  SetRowVisible(ImportantThreshold, show);
}

void
TopographyDisplayConfigPanel::LoadSelectedLayer() noexcept
{
  if (layers.empty())
    return;

  UpdateLabelRowVisibility();
  RebuildThresholdEnumsForMode();

  if (IsAllLayers()) {
    LoadValueEnum(ShapeThreshold, all_ceiling_shape);
    LoadValueEnum(LabelThreshold, all_ceiling_label);
    LoadValueEnum(ImportantThreshold, all_ceiling_important);
    SyncLabelThresholdLimits();
    return;
  }

  const TopographyFile *file = layers[selected_enum - 1];
  LoadThresholdEnum(ShapeThreshold, file->GetScaleThreshold(),
                    all_ceiling_shape);
  if (file->HasLabels()) {
    LoadThresholdEnum(LabelThreshold, file->GetLabelThreshold(),
                      all_ceiling_label);
    LoadThresholdEnum(ImportantThreshold,
                      file->GetImportantLabelThreshold(),
                      all_ceiling_important);
    SyncLabelThresholdLimits();
  }
}

void
TopographyDisplayConfigPanel::OnModified(DataField &df) noexcept
{
  if (IsDataField(Layer, df)) {
    selected_enum = GetValueEnum(Layer);
    RememberSelectedLayer();
    LoadSelectedLayer();
    return;
  }

  if (IsDataField(ShapeThreshold, df) ||
      IsDataField(LabelThreshold, df) ||
      IsDataField(ImportantThreshold, df)) {
    if (store == nullptr)
      return;

    if (IsAllLayers()) {
      CaptureAllLayerCeilings();
      SyncLabelThresholdLimits();
      CaptureAllLayerCeilings();
    } else {
      SyncLabelThresholdLimits();
    }

    ApplySelectedLayer();
    store->NotifyThresholdsChanged();
    ActionInterface::SendMapSettings(true);
  }
}

void
TopographyDisplayConfigPanel::Prepare(ContainerWindow &parent,
                                        const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  store = data_components != nullptr
    ? data_components->topography.get()
    : nullptr;

  if (store == nullptr || store->begin() == store->end()) {
    AddReadOnly(_("Topology layers"),
                _("Per-layer visibility thresholds from the map file "
                  "(topology.tpl). Load a map with vector topography to "
                  "adjust them."),
                _("No vector topography in the current map."));
    return;
  }

  for (auto &file : *store)
    layers.push_back(&file);

  RefreshRulerFactor();

  AddEnum(_("Layer"),
          _("Select a topography layer from the current map, or all "
            "layers. With all layers, thresholds act as a maximum: "
            "layers that already disappear earlier are left "
            "unchanged. Individual layers cannot exceed the all-layers "
            "ceilings."));
  auto &layer_field = (DataFieldEnum &)GetDataField(Layer);
  layer_field.AddChoice(ALL_LAYERS, _("All layers"), _("All layers"));
  for (unsigned i = 0; i < layers.size(); ++i)
    layer_field.AddChoice(i + 1, layers[i]->GetLayerName(),
                          layers[i]->GetLayerName());

  RestoreSelectedLayer(layer_field);

  const unsigned list_max = GetDefaultMaxThresholdUser();
  all_ceiling_shape = list_max;
  all_ceiling_label = list_max;
  all_ceiling_important = list_max;

  const char *unit_name =
    Units::GetUnitName(Units::GetUserDistanceUnit());

  auto add_threshold = [this, unit_name, list_max](const char *label,
                                                   const char *help,
                                                   unsigned value) {
    auto *control = AddEnum(label, help);
    auto &df = *(DataFieldEnum *)control->GetDataField();
    FillThresholdChoices(df, list_max, unit_name);
    df.SetValue(SnapThresholdChoice(value, list_max));
  };

  add_threshold(_("Shape threshold"),
                _("Maximum map scale (as on the map scale bar) at which "
                  "shapes are drawn. Larger values keep the layer visible "
                  "when more zoomed out. With all layers selected, this "
                  "is a ceiling only and does not raise lower per-layer "
                  "thresholds. The default maximum is 600 km "
                  "(300 for miles)."),
                all_ceiling_shape);

  add_threshold(_("Label threshold"),
                _("Maximum map scale (as on the map scale bar) at which "
                  "labels are drawn. May exceed the shape threshold so "
                  "labels can appear without shapes."),
                all_ceiling_label);

  add_threshold(_("Important label threshold"),
                _("Labels below this map scale use the default style "
                  "(smaller / less prominent). Cannot exceed the label "
                  "threshold."),
                all_ceiling_important);

  GetDataField(Layer).SetListener(this);
  GetDataField(ShapeThreshold).SetListener(this);
  GetDataField(LabelThreshold).SetListener(this);
  GetDataField(ImportantThreshold).SetListener(this);

  SyncLabelThresholdLimits();
  UpdateLabelRowVisibility();
  LoadSelectedLayer();

  AddButton(_("Apply custom settings"), [this]() {
    if (store == nullptr)
      return;

    const unsigned count = TopographySettings::ApplyCustomPreset(*store);
    if (count == 0) {
      Message::AddMessage(_("No matching topology layers"));
      return;
    }

    LoadSelectedLayer();
    TopographySettings::SaveFromStore(*store);
    ActionInterface::SendMapSettings(true);
    Message::AddMessage(_("Custom topology thresholds applied"));
  });

  AddButton(_("Reset layer to map default"), [this]() {
    if (store == nullptr || layers.empty())
      return;

    if (IsAllLayers())
      store->ResetAllLayerThresholds();
    else
      layers[selected_enum - 1]->ResetThresholds();

    LoadSelectedLayer();
    store->NotifyThresholdsChanged();
    TopographySettings::SaveFromStore(*store);
    ActionInterface::SendMapSettings(true);
  });

  AddButton(_("Reset all layers to map defaults"), [this]() {
    if (store == nullptr)
      return;

    store->ResetAllLayerThresholds();
    LoadSelectedLayer();
    TopographySettings::SaveFromStore(*store);
    ActionInterface::SendMapSettings(true);
  });
}

void
TopographyDisplayConfigPanel::Show(const PixelRect &rc) noexcept
{
  if (store != nullptr && store->begin() != store->end()) {
    ConfigPanel::BorrowExtraButton(2, _("Filter"), [](){
      dlgTopologyFilterShowModal();
    });

    /* Recompute ruler factor so orientation changes update the
       displayed numbers when the page is shown again. */
    RefreshRulerFactor();
    if (!layers.empty()) {
      /* Restore every Show: Prepare only runs once per dialog
         instance, and the Layer row needs an explicit refresh. */
      RestoreSelectedLayer((DataFieldEnum &)GetDataField(Layer));
      LoadSelectedLayer();
    }
  }

  RowFormWidget::Show(rc);
}

void
TopographyDisplayConfigPanel::Hide() noexcept
{
  RememberSelectedLayer();
  RowFormWidget::Hide();
  ConfigPanel::ReturnExtraButton(2);
}

bool
TopographyDisplayConfigPanel::Save(bool &_changed) noexcept
{
  if (store == nullptr || layers.empty())
    return true;

  ApplySelectedLayer();

  const char *old_value =
    Profile::Get(ProfileKeys::TopographyLayerOverrides);
  TopographySettings::SaveFromStore(*store);
  const char *new_value =
    Profile::Get(ProfileKeys::TopographyLayerOverrides);

  if (!StringIsEqual(old_value != nullptr ? old_value : "",
                     new_value != nullptr ? new_value : ""))
    _changed = true;

  return true;
}

std::unique_ptr<Widget>
CreateTopographyDisplayConfigPanel()
{
  return std::make_unique<TopographyDisplayConfigPanel>();
}
