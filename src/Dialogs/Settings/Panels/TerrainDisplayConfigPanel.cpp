// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TerrainDisplayConfigPanel.hpp"
#include "Dialogs/Settings/DisplaySettingConfigPanel.hpp"
#include "Terrain/TerrainDisplaySetting.hpp"
#include "Form/DataField/Listener.hpp"
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
#include "PageSetting.hpp"
#include "UIGlobals.hpp"
#include "Message.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Scissor.hpp"
#endif

namespace {

constexpr unsigned PageSettingTerrainStart = 0;

enum class NonCatalogRow : unsigned {
  Spacer = 0,
  Preview,
};

[[nodiscard]]
bool
IsExpertRow(unsigned control) noexcept
{
  switch (PageSettingId(PageSettingTerrainStart + control)) {
  case PageSettingId::TERRAIN_SLOPE_SHADING:
  case PageSettingId::TERRAIN_CONTRAST:
  case PageSettingId::TERRAIN_BRIGHTNESS:
  case PageSettingId::TERRAIN_CONTOURS:
    return true;
  default:
    return false;
  }
}

[[nodiscard]]
bool
NeedsListener(unsigned) noexcept
{
  return true;
}

} // namespace

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
  unsigned non_catalog_start = 0;

  /** Working copy edited by the form controls. */
  TerrainDisplaySetting::Bundle bundle;

  /** Snapshot at panel open (#1793: omit unchanged profile keys). */
  TerrainDisplaySetting::Bundle initial_bundle;

  void SyncBundleFromForm() noexcept;
  void ApplyBundleLive() noexcept;
  void UpdateTerrainPreview() noexcept;
  void ShowTerrainControls() noexcept;

  [[nodiscard]]
  unsigned
  NonCatalogControl(NonCatalogRow row) const noexcept
  {
    return non_catalog_start + unsigned(row);
  }

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
  DisplaySettingConfigPanel::SyncBundleFromForm(
    *this, bundle, PageSettingTerrainStart, PageSettingTerrainCount,
    TerrainDisplaySetting::Get, TerrainDisplaySetting::SetValue);
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
  using DisplaySettingConfigPanel::CatalogRow;

  const bool show = bundle.terrain.enable;
  SetRowVisible(CatalogRow(PageSettingId::TERRAIN_COLORS,
                           PageSettingTerrainStart), show);
  SetRowVisible(CatalogRow(PageSettingId::TERRAIN_SLOPE_SHADING,
                           PageSettingTerrainStart), show);
  SetRowVisible(CatalogRow(PageSettingId::TERRAIN_CONTRAST,
                           PageSettingTerrainStart), show);
  SetRowVisible(CatalogRow(PageSettingId::TERRAIN_BRIGHTNESS,
                           PageSettingTerrainStart), show);
  SetRowVisible(CatalogRow(PageSettingId::TERRAIN_CONTOURS,
                           PageSettingTerrainStart), show);
  if (have_terrain_preview) {
    SetRowVisible(NonCatalogControl(NonCatalogRow::Spacer), show);
    SetRowVisible(NonCatalogControl(NonCatalogRow::Preview), show);
  }
}

void
TerrainDisplayConfigPanel::UpdateTerrainPreview() noexcept
{
  if (!have_terrain_preview)
    return;

  ((TerrainPreviewWindow &)GetRow(NonCatalogControl(NonCatalogRow::Preview)))
    .SetSettings(bundle.terrain);
}

void
TerrainDisplayConfigPanel::OnModified(DataField &df) noexcept
{
  using DisplaySettingConfigPanel::CatalogRow;

  SyncBundleFromForm();

  if (IsDataField(CatalogRow(PageSettingId::TERRAIN_ENABLE,
                             PageSettingTerrainStart), df)) {
    Message::AddMessage(bundle.terrain.enable
                        ? _("Terrain shown")
                        : _("Terrain hidden"));
    ApplyBundleLive();
    ShowTerrainControls();
    UpdateTerrainPreview();
    return;
  }

  if (IsDataField(CatalogRow(PageSettingId::TOPOGRAPHY_ENABLE,
                             PageSettingTerrainStart), df)) {
    Message::AddMessage(bundle.topography_enabled
                        ? _("Topography shown")
                        : _("Topography hidden"));
    ApplyBundleLive();
    if (have_terrain_preview)
      ((TerrainPreviewWindow &)GetRow(NonCatalogControl(NonCatalogRow::Preview)))
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

  non_catalog_start = DisplaySettingConfigPanel::AddCatalogRows(
    *this, bundle, PageSettingTerrainStart, PageSettingTerrainCount,
    TerrainDisplaySetting::Get, TerrainDisplaySetting::GetValue,
    IsExpertRow, this, NeedsListener);

  have_terrain_preview = data_components->terrain != nullptr;
  DisplaySettingConfigPanel::AddNonCatalogRowsAfter(
    non_catalog_start, [this](unsigned) {
      if (!have_terrain_preview)
        return;

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
    });

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
