// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TerrainDisplayConfigPanel.hpp"
#include "Terrain/TerrainDisplaySetting.hpp"
#include "Form/DataField/Listener.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Boolean.hpp"
#include "Language/Language.hpp"
#include "Terrain/TerrainRenderer.hpp"
#include "Topography/TopographyRenderer.hpp"
#include "Topography/TopographyStore.hpp"
#include "Projection/MapWindowProjection.hpp"
#include "Components.hpp"
#include "DataComponents.hpp"
#include "Interface.hpp"
#include "ActionInterface.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Look/DialogLook.hpp"
#include "Look/MapLook.hpp"
#include "UIGlobals.hpp"
#include "Message.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Scissor.hpp"
#endif

enum ControlIndex {
  EnableTerrain,
  EnableTopography,
  TerrainColors,
  TerrainSlopeShading,
  TerrainContrast,
  TerrainBrightness,
  TerrainContours,
  TerrainSpacer,
  TerrainPreview,
};

class TerrainPreviewWindow : public PaintWindow {
  TerrainRenderer renderer;
  std::unique_ptr<TopographyRenderer> topo_renderer;
  bool topography_enabled;

public:
  TerrainPreviewWindow(const RasterTerrain &terrain,
                       const TopographyStore *topo_store,
                       const TopographyLook &topo_look,
                       bool _topography_enabled)
    :renderer(terrain),
     topography_enabled(_topography_enabled)
  {
#ifdef ENABLE_OPENGL
    renderer.SetQuantisationPixels(1);
#endif
    if (topo_store != nullptr)
      topo_renderer =
        std::make_unique<TopographyRenderer>(*topo_store, topo_look);
  }

  void SetSettings(const TerrainRendererSettings &settings) {
    renderer.SetSettings(settings);
    renderer.Flush();
    Invalidate();
  }

  void SetTopographyEnabled(bool enabled) {
    topography_enabled = enabled;
    Invalidate();
  }

  void OnPaint(Canvas &canvas) noexcept override;
};

class TerrainDisplayConfigPanel final
  : public RowFormWidget, DataFieldListener {

  bool have_terrain_preview = false;

  /** Working copy edited by the form controls. */
  TerrainDisplaySetting::Bundle bundle;

  /** Snapshot at panel open (#1793: omit unchanged profile keys). */
  TerrainDisplaySetting::Bundle initial_bundle;

  void SyncBundleFromForm() noexcept;
  void ApplyBundleLive() noexcept;
  void UpdateTerrainPreview() noexcept;
  void ShowTerrainControls() noexcept;

public:
  TerrainDisplayConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;

private:
  void OnModified(DataField &df) noexcept override;
};

void
TerrainDisplayConfigPanel::SyncBundleFromForm() noexcept
{
  using Id = PageSettingId;

  TerrainDisplaySetting::SetValue(bundle, Id::TERRAIN_ENABLE,
                                  GetValueBoolean(EnableTerrain) ? 1 : 0);
  TerrainDisplaySetting::SetValue(bundle, Id::TOPOGRAPHY_ENABLE,
                                  GetValueBoolean(EnableTopography) ? 1 : 0);
  TerrainDisplaySetting::SetValue(bundle, Id::TERRAIN_COLORS,
                                  int(GetValueEnum(TerrainColors)));
  TerrainDisplaySetting::SetValue(bundle, Id::TERRAIN_SLOPE_SHADING,
                                  int(GetValueEnum(TerrainSlopeShading)));
  TerrainDisplaySetting::SetValue(bundle, Id::TERRAIN_CONTRAST,
                                  GetValueInteger(TerrainContrast));
  TerrainDisplaySetting::SetValue(bundle, Id::TERRAIN_BRIGHTNESS,
                                  GetValueInteger(TerrainBrightness));
  TerrainDisplaySetting::SetValue(bundle, Id::TERRAIN_CONTOURS,
                                  int(GetValueEnum(TerrainContours)));
}

void
TerrainDisplayConfigPanel::ApplyBundleLive() noexcept
{
  TerrainDisplaySetting::ApplyLive(bundle);
  ActionInterface::SendMapSettings(true);
}

void
TerrainDisplayConfigPanel::ShowTerrainControls() noexcept
{
  const bool show = bundle.terrain.enable;
  SetRowVisible(TerrainColors, show);
  SetRowVisible(TerrainSlopeShading, show);
  SetRowVisible(TerrainContrast, show);
  SetRowVisible(TerrainBrightness, show);
  SetRowVisible(TerrainContours, show);
  if (have_terrain_preview) {
    SetRowVisible(TerrainSpacer, show);
    SetRowVisible(TerrainPreview, show);
  }
}

void
TerrainDisplayConfigPanel::UpdateTerrainPreview() noexcept
{
  if (!have_terrain_preview)
    return;

  ((TerrainPreviewWindow &)GetRow(TerrainPreview))
    .SetSettings(bundle.terrain);
}

void
TerrainDisplayConfigPanel::OnModified(DataField &df) noexcept
{
  SyncBundleFromForm();

  if (IsDataField(EnableTerrain, df)) {
    Message::AddMessage(bundle.terrain.enable
                        ? _("Terrain shown")
                        : _("Terrain hidden"));
    ApplyBundleLive();
    ShowTerrainControls();
    UpdateTerrainPreview();
    return;
  }

  if (IsDataField(EnableTopography, df)) {
    Message::AddMessage(bundle.topography_enabled
                        ? _("Topography shown")
                        : _("Topography hidden"));
    ApplyBundleLive();
    if (have_terrain_preview)
      ((TerrainPreviewWindow &)GetRow(TerrainPreview))
        .SetTopographyEnabled(bundle.topography_enabled);
    return;
  }

  ApplyBundleLive();
  UpdateTerrainPreview();
}

void
TerrainPreviewWindow::OnPaint(Canvas &canvas) noexcept
{
  const GlueMapWindow *map = UIGlobals::GetMap();
  if (map == nullptr)
    return;

  MapWindowProjection projection = map->VisibleProjection();
  if (!projection.IsValid()) {
    canvas.Clear(UIGlobals::GetDialogLook().background_color);
    return;
  }

  projection.SetScreenSize(canvas.GetSize());
  projection.SetScreenOrigin(canvas.GetRect().GetCenter());

  Angle sun_azimuth(Angle::Degrees(-45));
  if (renderer.GetSettings().slope_shading == SlopeShading::SUN &&
      CommonInterface::Calculated().sun_data_available)
    sun_azimuth = CommonInterface::Calculated().sun_azimuth;

  renderer.Generate(projection, sun_azimuth);

#ifdef ENABLE_OPENGL
  GLCanvasScissor scissor(canvas);
#endif

  renderer.Draw(canvas, projection);

  if (topography_enabled && topo_renderer)
    topo_renderer->Draw(canvas, projection);
}

void
TerrainDisplayConfigPanel::Prepare(ContainerWindow &parent,
                                   const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  TerrainDisplaySetting::ReadLive(bundle);

  const auto &terrain_enable =
    TerrainDisplaySetting::Get(PageSettingId::TERRAIN_ENABLE);
  AddBoolean(gettext(terrain_enable.label),
             gettext(terrain_enable.help_global),
             bundle.terrain.enable);
  GetDataField(EnableTerrain).SetListener(this);

  const auto &topography_enable =
    TerrainDisplaySetting::Get(PageSettingId::TOPOGRAPHY_ENABLE);
  AddBoolean(gettext(topography_enable.label),
             gettext(topography_enable.help_global),
             bundle.topography_enabled);
  GetDataField(EnableTopography).SetListener(this);

  const auto &colors =
    TerrainDisplaySetting::Get(PageSettingId::TERRAIN_COLORS);
  AddEnum(gettext(colors.label), gettext(colors.help_global),
          colors.choices, bundle.terrain.ramp);
  GetDataField(TerrainColors).SetListener(this);

  const auto &slope =
    TerrainDisplaySetting::Get(PageSettingId::TERRAIN_SLOPE_SHADING);
  AddEnum(gettext(slope.label), gettext(slope.help_global),
          slope.choices, unsigned(bundle.terrain.slope_shading));
  GetDataField(TerrainSlopeShading).SetListener(this);
  SetExpertRow(TerrainSlopeShading);

  const auto &contrast =
    TerrainDisplaySetting::Get(PageSettingId::TERRAIN_CONTRAST);
  AddInteger(gettext(contrast.label), gettext(contrast.help_global),
             "%d %%", "%d %%", contrast.int_min, contrast.int_max,
             contrast.int_step,
             TerrainDisplaySetting::GetValue(bundle,
                                             PageSettingId::TERRAIN_CONTRAST));
  GetDataField(TerrainContrast).SetListener(this);
  SetExpertRow(TerrainContrast);

  const auto &brightness =
    TerrainDisplaySetting::Get(PageSettingId::TERRAIN_BRIGHTNESS);
  AddInteger(gettext(brightness.label), gettext(brightness.help_global),
             "%d %%", "%d %%", brightness.int_min, brightness.int_max,
             brightness.int_step,
             TerrainDisplaySetting::GetValue(bundle,
                                             PageSettingId::TERRAIN_BRIGHTNESS));
  GetDataField(TerrainBrightness).SetListener(this);
  SetExpertRow(TerrainBrightness);

  const auto &contours =
    TerrainDisplaySetting::Get(PageSettingId::TERRAIN_CONTOURS);
  AddEnum(gettext(contours.label), gettext(contours.help_global),
          contours.choices, unsigned(bundle.terrain.contours));
  GetDataField(TerrainContours).SetListener(this);
  SetExpertRow(TerrainContours);

  have_terrain_preview = data_components->terrain != nullptr;
  if (have_terrain_preview) {
    AddSpacer();

    WindowStyle style;
    style.Border();

    const auto &map_look = UIGlobals::GetMapLook();
    auto preview = std::make_unique<TerrainPreviewWindow>(
      *data_components->terrain,
      data_components->topography.get(),
      map_look.topography,
      bundle.topography_enabled);
    preview->Create((ContainerWindow &)GetWindow(), {0, 0, 100, 100}, style);
    AddRemaining(std::move(preview));
  }

  ShowTerrainControls();
  UpdateTerrainPreview();
  SyncBundleFromForm();
  initial_bundle = bundle;
}

bool
TerrainDisplayConfigPanel::Save(bool &_changed) noexcept
{
  SyncBundleFromForm();
  TerrainDisplaySetting::ApplyLive(bundle);
  _changed |= TerrainDisplaySetting::SaveGlobal(bundle, initial_bundle);
  return true;
}

std::unique_ptr<Widget>
CreateTerrainDisplayConfigPanel()
{
  return std::make_unique<TerrainDisplayConfigPanel>();
}
