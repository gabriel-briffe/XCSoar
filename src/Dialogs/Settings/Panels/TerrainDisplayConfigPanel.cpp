// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TerrainDisplayConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Components.hpp"
#include "DataComponents.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Look/DialogLook.hpp"
#include "Look/MapLook.hpp"
#include "MapSettings.hpp"
#include "MapWindow/GlueMapWindow.hpp"
#include "Message.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Projection/MapWindowProjection.hpp"
#include "Terrain/TerrainRenderer.hpp"
#include "Terrain/TerrainSettings.hpp"
#include "Topography/TopographyRenderer.hpp"
#include "Topography/TopographyStore.hpp"
#include "UIGlobals.hpp"
#include "Widget/CreateWindowWidget.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "ui/window/ContainerWindow.hpp"
#include "ui/window/PaintWindow.hpp"
#include "ui/window/Window.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Scissor.hpp"
#endif

#include <functional>
#include <memory>

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
    /* always render at full resolution in the preview;
       the default idle-based quantisation would produce a
       blocky image while the user interacts with the dialog */
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

static short
ByteToPercent(short byte)
{
  return (byte * 200 + 100) / 510;
}

static short
PercentToByte(short percent)
{
  return (percent * 510 + 255) / 200;
}

static void
AddLinkedSwitch(GroupedListWidget &list, const char *caption,
                const char *help, bool &field,
                std::function<void()> after = {}) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.toggle = true;
  options.checked = field;
  options.help = help;
  list.AddItem(caption, [&list, &field, after = std::move(after)] {
    field = !field;
    if (after)
      after();
    if (list.UpdateValues())
      list.UpdateLayout();
  }, options);
}

void
TerrainPreviewWindow::OnPaint(Canvas &canvas) noexcept
{
  const GlueMapWindow *map = UIGlobals::GetMap();
  if (map == nullptr)
    return;

  MapWindowProjection projection = map->VisibleProjection();
  if (!projection.IsValid()) {
    /* TODO: initialise projection to middle of map instead of bailing
       out */
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
  /* enable clipping because the OpenGL terrain renderer uses a large
     texture that exceeds the window dimensions */
  GLCanvasScissor scissor(canvas);
#endif

  renderer.Draw(canvas, projection);

  if (topography_enabled && topo_renderer)
    topo_renderer->Draw(canvas, projection);
}

static constexpr StaticEnumChoice terrain_ramp_list[] = {
  { 0, N_("Low lands"), },
  { 1, N_("Mountainous"), },
  { 2, N_("Imhof 7"), },
  { 3, N_("Imhof 4"), },
  { 4, N_("Imhof 12"), },
  { 5, N_("Imhof Atlas"), },
  { 6, N_("ICAO"), },
  { 9, N_("Vibrant"), },
  { 7, N_("Grey"), },
  { 8, N_("White"), },
  {10, N_("Sandstone"), },
  {11, N_("Pastel"), },
  {12, N_("Italian Avioportolano VFR Chart"), },
  {13, N_("German DFS VFR Chart"), },
  {14, N_("French SIA VFR Chart"), },
  {15, N_("High Contrast"), },
  {16, N_("High Contrast low lands"), },
  {17, N_("Very low lands"), },
  nullptr
};

static constexpr StaticEnumChoice slope_shading_list[] = {
  { SlopeShading::OFF, N_("Off"), },
  { SlopeShading::FIXED, NC_("Setting", "Fixed (North-West)"), },
  { SlopeShading::SUN, N_("Sun"), },
  { SlopeShading::WIND, N_("Wind"), },
  { SlopeShading::TOP_LEFT, NC_("Setting", "Fixed (Top Left)"), },
  nullptr
};

static constexpr StaticEnumChoice contours_list[] = {
  { Contours::OFF, N_("Off"), NC_("Setting", "No contour lines"), },
  { Contours::MOUNTAINS, NC_("Setting", "Mountains"),
    N_("For steep mountain terrain, 256m minimum spacing"), },
  { Contours::HIGHLANDS, NC_("Setting", "Highlands"),
    N_("Medium density, with 64m minimum spacing"), },
  { Contours::LOWLANDS, NC_("Setting", "Lowlands"),
    N_("More line density for gentler slopes. 16m minimum spacing"), },
  { Contours::SUPERFINE, NC_("Setting", "Superfine"),
    N_("Maximum density contour lines down to 8m spacing"), },
  { Contours::FIXED_256, NC_("Setting", "Fixed 256m"),
    N_("Fixed 256m spacing, no zoom dependence"), },
  { Contours::FIXED_128, NC_("Setting", "Fixed 128m"),
    N_("Fixed 128m spacing, no zoom dependence"), },
  { Contours::FIXED_64, NC_("Setting", "Fixed 64m"),
    N_("Fixed 64m spacing, no zoom dependence"), },
  nullptr
};

std::unique_ptr<Widget>
CreateTerrainDisplayConfigPanel()
{
  const MapSettings &settings_map = CommonInterface::GetMapSettings();
  const TerrainRendererSettings &terrain = settings_map.terrain;

  struct Fields {
    TerrainRendererSettings terrain;
    TerrainRendererSettings initial;
    int contrast_percent;
    int brightness_percent;
    bool topography;
    std::unique_ptr<TerrainPreviewWindow> preview_window;
  };

  auto fields = std::make_shared<Fields>();
  fields->contrast_percent = ByteToPercent(terrain.contrast);
  fields->brightness_percent = ByteToPercent(terrain.brightness);
  fields->terrain = terrain;
  /* ByteToPercent ↔ PercentToByte is lossy for some values.  The
     snapshot is taken after that round trip so an untouched row does
     not write a missing profile key (#1793). */
  fields->terrain.contrast = PercentToByte(fields->contrast_percent);
  fields->terrain.brightness = PercentToByte(fields->brightness_percent);
  fields->initial = fields->terrain;
  fields->topography = settings_map.topography_enabled;

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);

  TerrainPreviewWindow *preview = nullptr;
  std::unique_ptr<CreateWindowWidget> preview_widget;
  if (data_components->terrain != nullptr) {
    const auto &map_look = UIGlobals::GetMapLook();
    fields->preview_window = std::make_unique<TerrainPreviewWindow>(
      *data_components->terrain, data_components->topography.get(),
      map_look.topography, settings_map.topography_enabled);
    preview = fields->preview_window.get();
    preview_widget = std::make_unique<CreateWindowWidget>(
      [fields](ContainerWindow &parent, const PixelRect &rc,
               WindowStyle style) {
        style.Border();
        auto window = std::move(fields->preview_window);
        window->Create(parent, rc, style);
        TerrainRendererSettings settings = fields->terrain;
        settings.contrast = PercentToByte(fields->contrast_percent);
        settings.brightness = PercentToByte(fields->brightness_percent);
        window->SetSettings(settings);
        window->SetTopographyEnabled(fields->topography);
        return std::unique_ptr<Window>(std::move(window));
      });
  }

  const auto terrain_shown = [fields, preview] {
    if (preview != nullptr && preview->IsDefined()) {
      TerrainRendererSettings settings = fields->terrain;
      settings.contrast = PercentToByte(fields->contrast_percent);
      settings.brightness = PercentToByte(fields->brightness_percent);
      preview->SetSettings(settings);
      preview->SetTopographyEnabled(fields->topography);
    }
    return fields->terrain.enable;
  };

  AddLinkedSwitch(*list, _("Terrain Display"),
                  _("Draw a digital elevation terrain on the map."),
                  fields->terrain.enable, [fields] {
    Message::AddMessage(fields->terrain.enable
                        ? _("Terrain shown")
                        : _("Terrain hidden"));
  });
  AddLinkedSwitch(*list, _("Topography display"),
                  _("Draw topographical features (roads, rivers, lakes etc.) on the map."),
                  fields->topography, [fields] {
    Message::AddMessage(fields->topography
                        ? _("Topography shown")
                        : _("Topography hidden"));
  });
  list->AddEnum(_("Terrain colors"),
                _("Defines the color ramp used in terrain rendering."),
                terrain_ramp_list, fields->terrain.ramp, false,
                terrain_shown);
  list->AddEnum(_("Slope shading"),
                _("The terrain can be shaded among slopes to indicate either "
                  "wind direction, sun position, a geographically fixed shading from "
                  "North-West, or a screen-relative fixed shading from top left."),
                slope_shading_list, fields->terrain.slope_shading, true,
                terrain_shown);
  list->AddInteger(_("Terrain contrast"),
                   _("Defines the amount of Phong shading in the terrain rendering. Use large values to emphasise terrain slope, smaller values if flying in steep mountains."),
                   "%d %%", "%d %%", 0, 100, 5,
                   fields->contrast_percent, true, terrain_shown);
  list->AddInteger(_("Terrain brightness"),
                   _("Defines the brightness (whiteness) of the terrain rendering. This controls the average illumination of the terrain."),
                   "%d %%", "%d %%", 0, 100, 5,
                   fields->brightness_percent, true, terrain_shown);
  list->AddEnum(_("Contours"),
                _("Draw contour lines on the terrain. Contour mode "
                  "controls density of contour lines."),
                contours_list, fields->terrain.contours, true,
                terrain_shown);
#ifdef ENABLE_OPENGL
  list->AddSwitch(_("GPU DEM spike"),
                  _("Experimental: sample fine DEM tiles on the GPU "
                    "(no ScanMap)."),
                  fields->terrain.gpu_dem_spike, true, terrain_shown);
#endif

  if (preview_widget != nullptr)
    list->AddWidgetGroup(nullptr, std::move(preview_widget), 0, true,
                         [fields] { return fields->terrain.enable; });

  list->SetSaveCallback([fields](bool &changed) {
    MapSettings &settings_map = CommonInterface::SetMapSettings();

    TerrainRendererSettings saved = fields->terrain;
    saved.contrast = PercentToByte(fields->contrast_percent);
    saved.brightness = PercentToByte(fields->brightness_percent);

    /* Always apply in-memory map settings.  Persist only when values
       differ from the panel-open snapshot so missing profile defaults
       stay absent (#1793). */
    settings_map.terrain = saved;
    if (saved != fields->initial) {
      Profile::Set(ProfileKeys::DrawTerrain, saved.enable);
      Profile::Set(ProfileKeys::TerrainContrast, saved.contrast);
      Profile::Set(ProfileKeys::TerrainBrightness, saved.brightness);
      Profile::Set(ProfileKeys::TerrainRamp, saved.ramp);
      Profile::SetEnum(ProfileKeys::SlopeShadingType, saved.slope_shading);
      Profile::SetEnum(ProfileKeys::TerrainContours, saved.contours);
#ifdef ENABLE_OPENGL
      Profile::Set(ProfileKeys::TerrainGpuDemSpike, saved.gpu_dem_spike);
#endif
      changed = true;
    }

    changed |= ConfigPanel::CommitSetting(changed,
                                          settings_map.topography_enabled,
                                          fields->topography,
                                          ProfileKeys::DrawTopography);
    return true;
  });

  return list;
}
