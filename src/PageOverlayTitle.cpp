// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "PageOverlayTitle.hpp"

#include "Language/Language.hpp"
#include "Weather/Features.hpp"
#include "Weather/Rasp/RaspStore.hpp"
#include "util/StaticString.hxx"
#include "util/StringBuilder.hxx"
#include "util/Compiler.h"

#ifdef HAVE_EDL
#include "Formatter/UserUnits.hpp"
#include "Weather/EDL/Levels.hpp"
#include "Weather/EDL/StateController.hpp"
#endif
#ifdef HAVE_HTTP
#include "DataGlobals.hpp"
#include "Interface.hpp"
#include "UIState.hpp"
#include "Weather/SkySight/SkySightClient.hpp"
#include "Weather/xctherm/XCThermCatalog.hpp"
#include "time/Convert.hxx"
#endif

void
AppendOverlayTitle(BasicStringBuilder<char> &builder,
                   const PageLayout &layout,
                   const RaspStore *rasp)
{
  switch (layout.overlay) {
  case PageLayout::Overlay::NONE:
    break;

  case PageLayout::Overlay::RASP:
    builder.Append(", RASP");
    if (rasp != nullptr &&
        layout.rasp_field >= 0 &&
        unsigned(layout.rasp_field) < rasp->GetItemCount()) {
      const auto &item = rasp->GetItemInfo(layout.rasp_field);
      const char *label = item.label != nullptr
        ? gettext(item.label)
        : item.name;
      if (label != nullptr && *label != '\0') {
        builder.Append(' ');
        builder.Append(label);
      }
    }
    break;

  case PageLayout::Overlay::EDL:
    builder.Append(", EDL");
#ifdef HAVE_EDL
    {
      EDL::EnsureInitialised();
      char alt[32];
      if (layout.edl_isobar > 0 &&
          EDL::IsSupportedIsobar(unsigned(layout.edl_isobar)))
        FormatUserAltitude(
          EDL::GetAltitudeForIsobar(unsigned(layout.edl_isobar)), alt);
      else
        FormatUserAltitude(EDL::GetAltitude(), alt);
      if (alt[0] != '\0') {
        builder.Append(' ');
        builder.Append(alt);
      }
    }
#endif
    break;

  case PageLayout::Overlay::XCTHERM:
    builder.Append(", XC Therm");
#ifdef HAVE_HTTP
    {
      builder.Append(' ');
      if (layout.xctherm_layer == PageLayout::XCTHERM_LAYER_AUTO)
        builder.Append(C_("Status", "Auto"));
      else {
        const auto &settings =
          CommonInterface::GetComputerSettings().weather.xctherm;
        const auto &region = XCTherm::GetRegion(settings.model);
        if (layout.xctherm_layer >= 0 &&
            unsigned(layout.xctherm_layer) < region.layer_count)
          builder.Append(
            gettext(region.layers[layout.xctherm_layer].short_label));
      }
    }
#endif
    break;

  case PageLayout::Overlay::SKYSIGHT:
    builder.Append(" | SkySight");
    if (!layout.skysight_overlay.empty()) {
      const char *label = layout.skysight_overlay.c_str();
#ifdef HAVE_HTTP
      if (const auto skysight = DataGlobals::GetSkySight();
          skysight != nullptr)
        if (const auto *layer = skysight->GetSelectedLayer(label);
            layer != nullptr)
          label = layer->name.c_str();
#endif
      builder.Append(": ");
      builder.Append(label);
    }
#ifdef HAVE_HTTP
    {
      /* Live satellite/rain only: last shown UTC + F/P fill. */
      const auto &cursor =
        CommonInterface::GetUIState().weather.skysight_cursor;
      if (cursor.displayed_time > 0) {
        const auto tm = GmTime(
          std::chrono::system_clock::from_time_t(
            time_t(cursor.displayed_time)));
        char time_buf[8];
        std::strftime(time_buf, sizeof(time_buf), "%H:%M", &tm);
        builder.Append(' ');
        builder.Append(time_buf);
        builder.Append(cursor.tiles_complete ? "F" : "P");
      }
    }
#endif
    break;

  case PageLayout::Overlay::RAINBOW:
    builder.Append(", Rainbow");
    if (layout.rainbow_time == PageLayout::RAINBOW_TIME_AUTO) {
      builder.Append(' ');
      builder.Append(C_("Status", "Auto"));
    }
    if (layout.rainbow_satellite) {
      builder.Append(' ');
      builder.Append(C_("Weather layer", "Sat"));
    }
    if (layout.rainbow_rain) {
      builder.Append(' ');
      builder.Append(C_("Weather layer", "Rain"));
    }
#ifdef HAVE_HTTP
    {
      const auto &cursor =
        CommonInterface::GetUIState().weather.rainbow_cursor;
      if (cursor.displayed_time > 0) {
        const auto tm = GmTime(
          std::chrono::system_clock::from_time_t(
            time_t(cursor.displayed_time)));
        char time_buf[8];
        std::strftime(time_buf, sizeof(time_buf), "%H:%M", &tm);
        builder.Append(' ');
        builder.Append(time_buf);
        builder.Append(cursor.tiles_complete ? "F" : "P");
      }
    }
#endif
    break;

  case PageLayout::Overlay::MAX:
    gcc_unreachable();
  }
}
