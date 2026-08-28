// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TopographyDisplayConfigPanel.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Form/DataField/Listener.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Float.hpp"
#include "Topography/TopographyStore.hpp"
#include "Topography/TopographyFile.hpp"
#include "Topography/TopographySettings.hpp"
#include "Components.hpp"
#include "DataComponents.hpp"
#include "ActionInterface.hpp"
#include "Message.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "util/StringCompare.hxx"

#include <algorithm>
#include <vector>

namespace {

constexpr double
ThresholdToNm(double threshold) noexcept
{
  return threshold / 1000.;
}

constexpr double
NmToThreshold(double nm) noexcept
{
  return nm * 1000.;
}

enum ControlIndex {
  Layer,
  ShapeThreshold,
  LabelThreshold,
  ImportantThreshold,
  ResetLayer,
  ResetAll,
};

} // namespace

class TopographyDisplayConfigPanel final
  : public RowFormWidget, DataFieldListener {

  TopographyStore *store = nullptr;
  std::vector<TopographyFile *> layers;
  unsigned selected_layer = 0;

  void ApplySelectedLayer() noexcept;
  void LoadSelectedLayer() noexcept;
  void SyncLabelThresholdLimits() noexcept;

public:
  TopographyDisplayConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;

protected:
  void OnModified(DataField &df) noexcept override;
};

void
TopographyDisplayConfigPanel::ApplySelectedLayer() noexcept
{
  if (selected_layer >= layers.size())
    return;

  const double shape = GetValueFloat(ShapeThreshold);
  const double label = std::min(GetValueFloat(LabelThreshold), shape);
  const double important =
    std::min(GetValueFloat(ImportantThreshold), shape);

  TopographyFile &file = *layers[selected_layer];
  file.SetThresholds(NmToThreshold(shape),
                     NmToThreshold(label),
                     NmToThreshold(important));
}

void
TopographyDisplayConfigPanel::SyncLabelThresholdLimits() noexcept
{
  const double shape = GetValueFloat(ShapeThreshold);

  for (unsigned i : {LabelThreshold, ImportantThreshold}) {
    auto &df = (DataFieldFloat &)GetDataField(i);
    df.SetMax(shape);

    const double value = std::min(GetValueFloat(i), shape);
    if (value != GetValueFloat(i))
      LoadValue(i, value);

    GetControl(i).RefreshDisplay();
  }
}

void
TopographyDisplayConfigPanel::LoadSelectedLayer() noexcept
{
  if (selected_layer >= layers.size())
    return;

  const TopographyFile &file = *layers[selected_layer];
  LoadValue(ShapeThreshold,
            ThresholdToNm(file.GetScaleThreshold()));
  LoadValue(LabelThreshold,
            ThresholdToNm(file.GetLabelThreshold()));
  LoadValue(ImportantThreshold,
            ThresholdToNm(file.GetImportantLabelThreshold()));
  SyncLabelThresholdLimits();
}

void
TopographyDisplayConfigPanel::OnModified(DataField &df) noexcept
{
  if (IsDataField(Layer, df)) {
    selected_layer = GetValueEnum(Layer);
    LoadSelectedLayer();
    return;
  }

  if (IsDataField(ShapeThreshold, df) ||
      IsDataField(LabelThreshold, df) ||
      IsDataField(ImportantThreshold, df)) {
    if (store == nullptr)
      return;

    SyncLabelThresholdLimits();
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

  AddEnum(_("Layer"),
          _("Select a topography layer from the "
            "current map."));
  auto &layer_field = (DataFieldEnum &)GetDataField(Layer);
  for (unsigned i = 0; i < layers.size(); ++i)
    layer_field.AddChoice(i, layers[i]->GetLayerName(),
                          layers[i]->GetLayerName());

  selected_layer = 0;
  layer_field.SetValue(0U);

  AddFloat(_("Shape threshold"),
           _("Maximum map scale in nautical miles at which shapes "
             "are drawn. Larger values keep the layer visible when "
             "more zoomed out."),
           "%.0f nm", "%.0f",
           0., 500., 5., false,
           ThresholdToNm(layers[0]->GetScaleThreshold()));

  AddFloat(_("Label threshold"),
           _("Maximum map scale in nautical miles at which labels "
             "are drawn. Cannot exceed the shape threshold."),
           "%.0f nm", "%.0f",
           0., 500., 5., false,
           ThresholdToNm(layers[0]->GetLabelThreshold()));

  AddFloat(_("Important label threshold"),
           _("Labels below this map scale use the default style "
             "(smaller / less prominent). Cannot exceed the shape "
             "threshold."),
           "%.0f nm", "%.0f",
           0., 500., 5., false,
           ThresholdToNm(layers[0]->GetImportantLabelThreshold()));

  GetDataField(Layer).SetListener(this);
  GetDataField(ShapeThreshold).SetListener(this);
  GetDataField(LabelThreshold).SetListener(this);
  GetDataField(ImportantThreshold).SetListener(this);

  SyncLabelThresholdLimits();

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
    if (store == nullptr || selected_layer >= layers.size())
      return;

    layers[selected_layer]->ResetThresholds();
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
