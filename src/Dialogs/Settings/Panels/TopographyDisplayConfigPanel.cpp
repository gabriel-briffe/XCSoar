// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TopographyDisplayConfigPanel.hpp"
#include "ActionInterface.hpp"
#include "Components.hpp"
#include "ConfigPanel.hpp"
#include "DataComponents.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Dialogs/Topography/TopographyDialogs.hpp"
#include "Form/DataField/Enum.hpp"
#include "Language/Language.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "Message.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Projection/MapWindowProjection.hpp"
#include "Topography/TopographyFile.hpp"
#include "Topography/TopographySettings.hpp"
#include "Topography/TopographyStore.hpp"
#include "UIGlobals.hpp"
#include "Units/Descriptor.hpp"
#include "Units/Units.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StaticString.hxx"
#include "util/StringCompare.hxx"
#include "util/StringFormat.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

/** Enum value 0 = all layers; 1..n = layers[n-1]. */
constexpr unsigned ALL_LAYERS = 0;

/**
 * Last layer chosen on the Topology settings page (kept for the
 * process lifetime so reopening the menu restores the selection).
 */
bool last_layer_all = true;
StaticString<64> last_layer_name;

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

struct TopographyFields {
  TopographyStore *store = nullptr;
  std::vector<TopographyFile *> layers;
  unsigned selected_enum = ALL_LAYERS;
  double map_scale_to_ruler = 8.;
  unsigned all_ceiling_shape = 0;
  unsigned all_ceiling_label = 0;
  unsigned all_ceiling_important = 0;
  unsigned shape_user = 0;
  unsigned label_user = 0;
  unsigned important_user = 0;

  [[gnu::pure]]
  bool IsAllLayers() const noexcept {
    return selected_enum == ALL_LAYERS;
  }

  [[gnu::pure]]
  bool AnyLayerHasLabels() const noexcept {
    for (const TopographyFile *file : layers)
      if (file->HasLabels())
        return true;
    return false;
  }

  [[gnu::pure]]
  bool LabelsVisible() const noexcept {
    if (IsAllLayers())
      return AnyLayerHasLabels();
    return selected_enum >= 1 && selected_enum <= layers.size() &&
      layers[selected_enum - 1]->HasLabels();
  }

  void RefreshRulerFactor() noexcept {
    map_scale_to_ruler = GetMapScaleToRulerFactor();
    if (map_scale_to_ruler <= 0.)
      map_scale_to_ruler = 8.;
  }

  [[gnu::pure]]
  double ToRulerUser(double map_scale_m) const noexcept {
    return Units::ToUserDistance(map_scale_m * map_scale_to_ruler);
  }

  [[gnu::pure]]
  double FromRulerUser(double ruler_user) const noexcept {
    return Units::ToSysDistance(ruler_user) / map_scale_to_ruler;
  }

  unsigned ShapeMax() const noexcept {
    return IsAllLayers() ? GetDefaultMaxThresholdUser() : all_ceiling_shape;
  }

  unsigned LabelMax() const noexcept {
    return IsAllLayers() ? GetDefaultMaxThresholdUser() : all_ceiling_label;
  }

  unsigned ImportantMax() const noexcept {
    return IsAllLayers()
      ? GetDefaultMaxThresholdUser()
      : all_ceiling_important;
  }

  void RememberSelectedLayer() noexcept {
    if (IsAllLayers()) {
      last_layer_all = true;
      last_layer_name.clear();
      Profile::Set(ProfileKeys::TopographySelectedLayer, "");
      return;
    }

    last_layer_all = false;
    last_layer_name = layers[selected_enum - 1]->GetLayerName();
    Profile::Set(ProfileKeys::TopographySelectedLayer,
                 last_layer_name.c_str());
  }

  void RestoreSelectedLayer() noexcept {
    selected_enum = ALL_LAYERS;

    StaticString<64> wanted = last_layer_name;
    bool want_all = last_layer_all;

    if (want_all && wanted.empty()) {
      const char *saved = Profile::Get(ProfileKeys::TopographySelectedLayer);
      if (saved != nullptr && saved[0] != '\0') {
        want_all = false;
        wanted = saved;
      }
    }

    if (!want_all && !wanted.empty()) {
      for (unsigned i = 0; i < layers.size(); ++i) {
        if (StringIsEqual(layers[i]->GetLayerName(), wanted.c_str())) {
          selected_enum = i + 1;
          break;
        }
      }
    }
  }

  void SetLayerThresholds(TopographyFile &file,
                          double shape_m, double label_m,
                          double important_m) noexcept {
    if (!file.HasLabels()) {
      file.SetThresholds(shape_m,
                         file.GetLabelThreshold(),
                         file.GetImportantLabelThreshold());
      return;
    }

    file.SetThresholds(shape_m, label_m,
                       std::min(important_m, label_m));
  }

  void ApplySelectedLayer() noexcept {
    if (layers.empty())
      return;

    const double shape_m = FromRulerUser(shape_user);
    const double label_m = FromRulerUser(label_user);
    const double important_m = FromRulerUser(important_user);

    if (IsAllLayers()) {
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

  void SyncLabelThresholdLimits() noexcept {
    if (!LabelsVisible())
      return;

    if (important_user > label_user)
      important_user = label_user;
  }

  void CaptureAllLayerCeilings() noexcept {
    all_ceiling_shape = shape_user;
    all_ceiling_label = label_user;
    all_ceiling_important = important_user;

    if (all_ceiling_important > all_ceiling_label)
      all_ceiling_important = all_ceiling_label;
  }

  void LoadSelectedLayer() noexcept {
    if (layers.empty())
      return;

    if (IsAllLayers()) {
      shape_user = all_ceiling_shape;
      label_user = all_ceiling_label;
      important_user = all_ceiling_important;
      SyncLabelThresholdLimits();
      return;
    }

    const TopographyFile *file = layers[selected_enum - 1];
    shape_user = SnapThresholdChoice(ToRulerUser(file->GetScaleThreshold()),
                                     all_ceiling_shape);
    if (file->HasLabels()) {
      label_user = SnapThresholdChoice(ToRulerUser(file->GetLabelThreshold()),
                                       all_ceiling_label);
      important_user = SnapThresholdChoice(
        ToRulerUser(file->GetImportantLabelThreshold()),
        all_ceiling_important);
      SyncLabelThresholdLimits();
    }
  }

  void OnThresholdChanged() noexcept {
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

  void FillLayerField(DataFieldEnum &df) const noexcept {
    df.ClearChoices();
    df.AddChoice(ALL_LAYERS, _("All layers"), _("All layers"));
    for (unsigned i = 0; i < layers.size(); ++i)
      df.AddChoice(i + 1, layers[i]->GetLayerName(),
                   layers[i]->GetLayerName());
    df.SetValue(selected_enum);
  }
};

} // namespace

std::unique_ptr<Widget>
CreateTopographyDisplayConfigPanel()
{
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

  TopographyStore *store = data_components != nullptr
    ? data_components->topography.get()
    : nullptr;

  if (store == nullptr || store->begin() == store->end()) {
    list->AddValue(_("Topology layers"),
                   _("Per-layer visibility thresholds from the map file "
                     "(topology.tpl). Load a map with vector topography to "
                     "adjust them."),
                   [](GroupedListWidget::ValueState &state) {
                     state.text =
                       _("No vector topography in the current map.");
                   });
    return list;
  }

  auto fields = std::make_shared<TopographyFields>();
  fields->store = store;
  for (auto &file : *store)
    fields->layers.push_back(&file);

  fields->RefreshRulerFactor();
  fields->RestoreSelectedLayer();

  const unsigned list_max = GetDefaultMaxThresholdUser();
  fields->all_ceiling_shape = list_max;
  fields->all_ceiling_label = list_max;
  fields->all_ceiling_important = list_max;
  fields->LoadSelectedLayer();

  auto *page = list.get();
  const char *unit_name =
    Units::GetUnitName(Units::GetUserDistanceUnit());

  const char *const layer_help =
    _("Select a topography layer from the current map, or all "
      "layers. With all layers, thresholds act as a maximum: "
      "layers that already disappear earlier are left "
      "unchanged. Individual layers cannot exceed the all-layers "
      "ceilings.");

  list->AddValue(_("Layer"), layer_help,
                 [fields](GroupedListWidget::ValueState &state) {
                   DataFieldEnum df;
                   fields->FillLayerField(df);
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page, layer_help] {
                   DataFieldEnum df;
                   fields->FillLayerField(df);
                   if (!ComboPicker(_("Layer"), df, layer_help) ||
                       df.GetValue() == fields->selected_enum)
                     return;
                   fields->selected_enum = df.GetValue();
                   fields->RememberSelectedLayer();
                   fields->LoadSelectedLayer();
                   if (page->UpdateValues())
                     page->UpdateLayout();
                 });

  const char *const shape_help =
    _("Maximum map scale (as on the map scale bar) at which "
      "shapes are drawn. Larger values keep the layer visible "
      "when more zoomed out. With all layers selected, this "
      "is a ceiling only and does not raise lower per-layer "
      "thresholds. The default maximum is 600 km "
      "(300 for miles).");

  list->AddValue(_("Shape threshold"), shape_help,
                 [fields, unit_name](GroupedListWidget::ValueState &state) {
                   DataFieldEnum df;
                   FillThresholdChoices(df, fields->ShapeMax(), unit_name);
                   df.SetValue(SnapThresholdChoice(fields->shape_user,
                                                   fields->ShapeMax()));
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page, unit_name, shape_help] {
                   DataFieldEnum df;
                   FillThresholdChoices(df, fields->ShapeMax(), unit_name);
                   df.SetValue(SnapThresholdChoice(fields->shape_user,
                                                   fields->ShapeMax()));
                   if (!ComboPicker(_("Shape threshold"), df, shape_help))
                     return;
                   fields->shape_user = df.GetValue();
                   fields->OnThresholdChanged();
                   page->UpdateValues();
                 });

  const char *const label_help =
    _("Maximum map scale (as on the map scale bar) at which "
      "labels are drawn. May exceed the shape threshold so "
      "labels can appear without shapes.");

  list->AddValue(_("Label threshold"), label_help,
                 [fields, unit_name](GroupedListWidget::ValueState &state) {
                   state.hidden = !fields->LabelsVisible();
                   DataFieldEnum df;
                   FillThresholdChoices(df, fields->LabelMax(), unit_name);
                   df.SetValue(SnapThresholdChoice(fields->label_user,
                                                   fields->LabelMax()));
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page, unit_name, label_help] {
                   DataFieldEnum df;
                   FillThresholdChoices(df, fields->LabelMax(), unit_name);
                   df.SetValue(SnapThresholdChoice(fields->label_user,
                                                   fields->LabelMax()));
                   if (!ComboPicker(_("Label threshold"), df, label_help))
                     return;
                   fields->label_user = df.GetValue();
                   fields->OnThresholdChanged();
                   if (page->UpdateValues())
                     page->UpdateLayout();
                 });

  const char *const important_help =
    _("Labels below this map scale use the default style "
      "(smaller / less prominent). Cannot exceed the label "
      "threshold.");

  list->AddValue(_("Important label threshold"), important_help,
                 [fields, unit_name](GroupedListWidget::ValueState &state) {
                   state.hidden = !fields->LabelsVisible();
                   DataFieldEnum df;
                   FillThresholdChoices(df, fields->ImportantMax(),
                                        unit_name);
                   df.SetValue(SnapThresholdChoice(fields->important_user,
                                                   fields->ImportantMax()));
                   state.text = df.GetAsDisplayString();
                 },
                 [fields, page, unit_name, important_help] {
                   DataFieldEnum df;
                   FillThresholdChoices(df, fields->ImportantMax(),
                                        unit_name);
                   df.SetValue(SnapThresholdChoice(fields->important_user,
                                                   fields->ImportantMax()));
                   if (!ComboPicker(_("Important label threshold"), df,
                                    important_help))
                     return;
                   fields->important_user = df.GetValue();
                   fields->OnThresholdChanged();
                   page->UpdateValues();
                 });

  list->AddButton(_("Apply custom settings"), [fields, page] {
    if (fields->store == nullptr)
      return;

    const unsigned count =
      TopographySettings::ApplyCustomPreset(*fields->store);
    if (count == 0) {
      Message::AddMessage(_("No matching topology layers"));
      return;
    }

    fields->LoadSelectedLayer();
    TopographySettings::SaveFromStore(*fields->store);
    ActionInterface::SendMapSettings(true);
    page->UpdateValues();
    Message::AddMessage(_("Custom topology thresholds applied"));
  });

  list->AddButton(_("Reset layer to map default"), [fields, page] {
    if (fields->store == nullptr || fields->layers.empty())
      return;

    if (fields->IsAllLayers())
      fields->store->ResetAllLayerThresholds();
    else
      fields->layers[fields->selected_enum - 1]->ResetThresholds();

    fields->LoadSelectedLayer();
    fields->store->NotifyThresholdsChanged();
    TopographySettings::SaveFromStore(*fields->store);
    ActionInterface::SendMapSettings(true);
    page->UpdateValues();
  });

  list->AddButton(_("Reset all layers to map defaults"), [fields, page] {
    if (fields->store == nullptr)
      return;

    fields->store->ResetAllLayerThresholds();
    fields->LoadSelectedLayer();
    TopographySettings::SaveFromStore(*fields->store);
    ActionInterface::SendMapSettings(true);
    page->UpdateValues();
  });

  list->SetVisibilityCallback([fields, page](bool visible) {
    if (visible) {
      ConfigPanel::BorrowExtraButton(2, _("Filter"), []() {
        dlgTopologyFilterShowModal();
      });

      fields->RefreshRulerFactor();
      if (!fields->layers.empty()) {
        fields->RestoreSelectedLayer();
        fields->LoadSelectedLayer();
        page->UpdateValues();
      }
    } else {
      fields->RememberSelectedLayer();
      ConfigPanel::ReturnExtraButton(2);
    }
  });

  list->SetSaveCallback([fields](bool &changed) {
    if (fields->store == nullptr || fields->layers.empty())
      return true;

    fields->ApplySelectedLayer();

    const char *old_value =
      Profile::Get(ProfileKeys::TopographyLayerOverrides);
    TopographySettings::SaveFromStore(*fields->store);
    const char *new_value =
      Profile::Get(ProfileKeys::TopographyLayerOverrides);

    if (!StringIsEqual(old_value != nullptr ? old_value : "",
                       new_value != nullptr ? new_value : ""))
      changed = true;

    return true;
  });

  return list;
}
