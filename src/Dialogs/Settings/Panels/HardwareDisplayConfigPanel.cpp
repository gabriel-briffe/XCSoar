// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "HardwareDisplayConfigPanel.hpp"
#include "ui/canvas/Features.hpp" // for DRAW_MOUSE_CURSOR
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Form/DataField/Enum.hpp"
#include "Hardware/RotateDisplay.hpp"
#include "Interface.hpp"
#include "MainWindow.hpp"
#include "LogFile.hpp"
#include "Language/Language.hpp"
#include "Widget/RowFormWidget.hpp"
#include "UIGlobals.hpp"
#include "ActionInterface.hpp"
#include "util/Macros.hpp"
#include "util/StaticString.hxx"
#include "UISettings.hpp"
#include "UtilsSettings.hpp"

#include <cassert>

#ifdef USE_POLL_EVENT
#include "ui/event/Globals.hpp"
#include "ui/event/Queue.hpp"
#endif

enum ControlIndex {
  CustomDPI,
  MapOrientation,
  AppDisplayType,
#ifdef DRAW_MOUSE_CURSOR
  CursorSize,
  CursorInverted,
#endif
};

static constexpr StaticEnumChoice display_orientation_list[] = {
  { DisplayOrientation::DEFAULT, N_("Default") },
  { DisplayOrientation::PORTRAIT, N_("Portrait") },
  { DisplayOrientation::LANDSCAPE, N_("Landscape") },
  { DisplayOrientation::REVERSE_PORTRAIT, N_("Reverse Portrait") },
  { DisplayOrientation::REVERSE_LANDSCAPE, N_("Reverse Landscape") },
  nullptr
};

static constexpr StaticEnumChoice display_type_list[] = {
  { DisplayType::LCD, NC_("Setting", "LCD"),
    N_("Conventional LCD or OLED. Full scrolling animations.") },
  { DisplayType::E_INK, NC_("Setting", "E-ink"),
    N_("Monochrome electronic paper. Disables kinetic and smooth "
       "scrolling.") },
  { DisplayType::COLOR_E_INK, NC_("Setting", "Color e-ink"),
    N_("Color electronic paper. Disables kinetic and smooth "
       "scrolling like monochrome e-ink.") },
  nullptr
};

static_assert(ARRAY_SIZE(display_type_list) ==
              unsigned(DisplayType::COUNT) + 1,
              "display_type_list must match DisplayType::COUNT");

class HardwareDisplayConfigPanel final : public RowFormWidget {
public:
  HardwareDisplayConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
HardwareDisplayConfigPanel::Prepare(ContainerWindow &parent,
                                    const PixelRect &rc) noexcept
{
  const UISettings &settings = CommonInterface::GetUISettings();

  RowFormWidget::Prepare(parent, rc);

  WndProperty *wp_dpi = AddEnum(_("Display Resolution"),
                                _("The display resolution is used to adapt line widths, "
                                  "font size, landable size and more."));
  if (wp_dpi != nullptr) {
    static constexpr unsigned dpi_choices[] = {
      120, 160, 240, 260, 280, 300, 340, 360, 400, 420, 520,
    };
    const unsigned *dpi_choices_end =
      dpi_choices + sizeof(dpi_choices) / sizeof(dpi_choices[0]);

    DataFieldEnum &df = *(DataFieldEnum *)wp_dpi->GetDataField();
    df.AddChoice(0, _("Automatic"));
    for (const unsigned *dpi = dpi_choices; dpi != dpi_choices_end; ++dpi) {
      StaticString<20> buffer;
      buffer.Format(_("%u dpi"), *dpi);
      df.AddChoice(*dpi, buffer);
    }
    df.SetValue(settings.custom_dpi);
    wp_dpi->RefreshDisplay();
  }
  SetExpertRow(CustomDPI);

  if (Display::RotateSupported())
    AddEnum(_("Display orientation"),
            _("Rotate the display on devices that support it."),
            display_orientation_list,
            (unsigned)settings.display.orientation);
  else
    AddDummy();

  AddEnum(C_("Setting", "Display type"),
          _("Select the display technology. E-ink modes disable kinetic "
            "and smooth scrolling for slow refresh screens."),
          display_type_list,
          (unsigned)settings.display.display_type);
  SetExpertRow(AppDisplayType);

#ifdef DRAW_MOUSE_CURSOR
  AddInteger(_("Cursor zoom"), _("Cursor zoom factor"), "%d x", "%d x", 1, 10, 1,
             (unsigned)settings.display.cursor_size);
  AddBoolean(_("Invert cursor color"), _("Enable black cursor"),
             settings.display.invert_cursor_colors);
#endif
}

bool
HardwareDisplayConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  UISettings &settings = CommonInterface::SetUISettings();

  if (SaveValueEnum(CustomDPI, ProfileKeys::CustomDPI,
                    settings.custom_dpi))
    require_restart = changed = true;

  bool orientation_changed = false;
  if (Display::RotateSupported()) {
    orientation_changed =
      SaveValueEnum(MapOrientation, ProfileKeys::MapOrientation,
                    settings.display.orientation);
    changed |= orientation_changed;
  }

  if (SaveValueEnum(AppDisplayType, ProfileKeys::DisplayType,
                    settings.display.display_type)) {
    changed = true;
    SetDisplayType(settings.display.display_type);
  }

#ifdef DRAW_MOUSE_CURSOR
  changed |= SaveValueInteger(CursorSize, ProfileKeys::CursorSize,
                              settings.display.cursor_size);
  CommonInterface::main_window->SetCursorSize(settings.display.cursor_size);

  changed |= SaveValue(CursorInverted, ProfileKeys::CursorColorsInverted,
                       settings.display.invert_cursor_colors);
  CommonInterface::main_window->SetCursorColorsInverted(
    settings.display.invert_cursor_colors);
#endif

  if (orientation_changed) {
    assert(Display::RotateSupported());

    if (!Display::Rotate(settings.display.orientation))
      LogString("Display rotation failed");

#ifdef USE_POLL_EVENT
    UI::event_queue->SetDisplayOrientation(settings.display.orientation);
#endif

    CommonInterface::main_window->CheckResize();
  }

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateHardwareDisplayConfigPanel()
{
  return std::make_unique<HardwareDisplayConfigPanel>();
}
