// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Widget/RowFormWidget.hpp"
#include "Airspace/AirspaceClass.hpp"
#include "Airspace/AirspaceClass.hpp"
#include "Dialogs/Airspace/Airspace.hpp"
#include "Renderer/AirspaceRendererSettings.hpp"

class AirspaceClassRendererSettingsPanel:
  public RowFormWidget
{
  enum ControlIndex {
    BorderColor,
    FillColor,
    FillBrush,
    BorderWidth,
    FillMode,
  };

  bool border_color_changed;
  bool fill_color_changed;
  bool fill_brush_changed;
  AirspaceClass type;
  PageAirspaceRendererSettingsContext page_context;
  AirspaceClassRendererSettings settings;

public:
  AirspaceClassRendererSettingsPanel(
    AirspaceClass type,
    PageAirspaceRendererSettingsContext _page_context = {}) noexcept;

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;

private:
  void FillAirspaceClasses() noexcept;
};
