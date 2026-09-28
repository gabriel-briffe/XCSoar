// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "HardwareDisplayConfigPanel.hpp"
#include "ui/canvas/Features.hpp" // for DRAW_MOUSE_CURSOR
#include "ConfigListPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Hardware/DisplayBrightness.hpp"
#include "Hardware/RotateDisplay.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "LogFile.hpp"
#include "MainWindow.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UISettings.hpp"
#include "UtilsSettings.hpp"
#include "util/Macros.hpp"
#include "util/StaticString.hxx"

#include <vector>

#ifdef USE_POLL_EVENT
#include "ui/event/Globals.hpp"
#include "ui/event/Queue.hpp"
#endif

#if defined(KOBO) || (defined(__linux__) && !defined(ANDROID))
#define HAVE_DISPLAY_BRIGHTNESS_CONTROL
#endif

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

static constexpr StaticEnumChoice display_orientation_list[] = {
  { DisplayOrientation::DEFAULT, N_("Default") },
  { DisplayOrientation::PORTRAIT, N_("Portrait") },
  { DisplayOrientation::LANDSCAPE, N_("Landscape") },
  { DisplayOrientation::REVERSE_PORTRAIT, N_("Reverse Portrait") },
  { DisplayOrientation::REVERSE_LANDSCAPE, N_("Reverse Landscape") },
  nullptr
};

static constexpr unsigned dpi_choices[] = {
  120, 160, 240, 260, 280, 300, 340, 360, 400, 420, 520,
};

/** The screen: its kind, its resolution, and how bright it is. */
class HardwareDisplayConfigPanel final : public ConfigListPanel {
  DisplayType display_type;
  unsigned custom_dpi;
  DisplayOrientation orientation;
#ifdef HAVE_DISPLAY_BRIGHTNESS_CONTROL
  std::unique_ptr<DisplayBrightness> brightness;
  int brightness_percent = 0;
  bool brightness_writable = false;
#endif
#ifdef DRAW_MOUSE_CURSOR
  int cursor_size;
  bool invert_cursor;
#endif

  void PickDpi() noexcept;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
HardwareDisplayConfigPanel::PickDpi() noexcept
{
  struct Choice {
    unsigned dpi;
    StaticString<20> label;
  };

  std::vector<Choice> items;
  items.push_back({});
  items.back().dpi = 0;
  items.back().label = _("Automatic");

  for (unsigned dpi : dpi_choices) {
    Choice choice;
    choice.dpi = dpi;
    choice.label.Format(_("%u dpi"), dpi);
    items.push_back(std::move(choice));
  }

  std::vector<PickerChoice> choices;
  choices.reserve(items.size());
  int current = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].dpi == custom_dpi)
      current = int(i);
    choices.push_back({items[i].label.c_str()});
  }

  const char *const help =
    _("The display resolution is used to adapt line widths, "
      "font size, landable size and more.");
  const int picked = PickChoice(_("Display resolution"), help,
                                choices, current);
  if (picked < 0 || items[picked].dpi == custom_dpi)
    return;

  custom_dpi = items[picked].dpi;
  Refresh();
}

void
HardwareDisplayConfigPanel::LoadSettings() noexcept
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  display_type = ui_settings.display.display_type;
  custom_dpi = ui_settings.custom_dpi;
  orientation = ui_settings.display.orientation;

#ifdef HAVE_DISPLAY_BRIGHTNESS_CONTROL
  brightness = DisplayBrightness::Detect();
  brightness_writable = brightness != nullptr && brightness->IsWritable();
  brightness_percent = brightness == nullptr ? 0
    : static_cast<int>(brightness->GetBrightnessPercent());
#endif

#ifdef DRAW_MOUSE_CURSOR
  cursor_size = ui_settings.display.cursor_size;
  invert_cursor = ui_settings.display.invert_cursor_colors;
#endif
}

void
HardwareDisplayConfigPanel::Fill() noexcept
{
  AddGroup();

  if (IsExpert()) {
    AddEnumItem(C_("Setting", "Display type"),
                _("Select the display technology. E-ink modes disable kinetic "
                  "and smooth scrolling for slow refresh screens."),
                display_type_list, display_type);

    StaticString<20> dpi_text;
    if (custom_dpi == 0)
      dpi_text = _("Automatic");
    else
      dpi_text.Format(_("%u dpi"), custom_dpi);

    AddItem(_("Display resolution"), [this](){ PickDpi(); },
            {.value = dpi_text.c_str(), .chevron = true,
             .help = _("The display resolution is used to adapt line widths, "
                       "font size, landable size and more.")});
  }

  if (Display::RotateSupported())
    AddEnumItem(_("Display orientation"),
                _("Rotate the display on devices that support it."),
                display_orientation_list, orientation);

#ifdef HAVE_DISPLAY_BRIGHTNESS_CONTROL
  if (brightness_writable)
    AddPercentItem(_("Screen brightness"),
                   _("Adjust the screen brightness."),
                   0, 100, 5, brightness_percent);
  else if (brightness != nullptr) {
    StaticString<16> brightness_text;
    brightness_text.Format("%d %%", brightness_percent);
    AddItem(_("Screen brightness"),
            {.value = brightness_text.c_str(),
             .help = _("Screen brightness is read-only because writing "
                       "requires additional permissions."),
             .disabled = true,
             .selectable_when_disabled = true});
  }
#endif

#ifdef DRAW_MOUSE_CURSOR
  StaticString<16> cursor_text;
  cursor_text.Format("%d x", cursor_size);
  AddItem(_("Cursor zoom"), [this](){
    if (PickNumber(_("Cursor zoom"), _("Cursor zoom factor"),
                   1, 10, 1, cursor_size,
                   [](StaticString<32> &s, int v){ s.Format("%d x", v); }))
      Refresh();
  }, {.value = cursor_text.c_str(), .chevron = true,
      .help = _("Cursor zoom factor")});

  AddToggleItem(_("Invert cursor color"), _("Enable black cursor"),
                invert_cursor);
#endif
}

bool
HardwareDisplayConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  UISettings &ui_settings = CommonInterface::SetUISettings();

  if (Profile::Update(ProfileKeys::DisplayType,
                      ui_settings.display.display_type, display_type)) {
    changed = true;
    SetDisplayType(ui_settings.display.display_type);
  }

  if (Profile::Update(ProfileKeys::CustomDPI, ui_settings.custom_dpi,
                      custom_dpi)) {
    changed = true;
    require_restart = true;
  }

  if (Display::RotateSupported() &&
      Profile::Update(ProfileKeys::MapOrientation,
                      ui_settings.display.orientation, orientation)) {
    changed = true;
    if (!Display::Rotate(ui_settings.display.orientation))
      LogString("Display rotation failed");
#ifdef SOFTWARE_ROTATE_DISPLAY
    CommonInterface::main_window->SetDisplayOrientation(
        ui_settings.display.orientation);
#endif
#ifdef USE_POLL_EVENT
    UI::event_queue->SetDisplayOrientation(ui_settings.display.orientation);
#endif
    CommonInterface::main_window->CheckResize();
  }

#ifdef HAVE_DISPLAY_BRIGHTNESS_CONTROL
  if (brightness_writable &&
      static_cast<unsigned>(brightness_percent) !=
      brightness->GetBrightnessPercent())
    brightness->SetBrightnessPercent(
      static_cast<unsigned>(brightness_percent));
#endif

#ifdef DRAW_MOUSE_CURSOR
  if (Profile::Update(ProfileKeys::CursorSize,
                      ui_settings.display.cursor_size,
                      static_cast<uint8_t>(cursor_size)))
    changed = true;
  CommonInterface::main_window->SetCursorSize(
    ui_settings.display.cursor_size);

  if (Profile::Update(ProfileKeys::CursorColorsInverted,
                      ui_settings.display.invert_cursor_colors,
                      invert_cursor))
    changed = true;
  CommonInterface::main_window->SetCursorColorsInverted(
    ui_settings.display.invert_cursor_colors);
#endif

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateHardwareDisplayConfigPanel()
{
  return std::make_unique<HardwareDisplayConfigPanel>();
}
