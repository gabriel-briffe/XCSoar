// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "DisplayConfigPanel.hpp"
#include "ui/canvas/Features.hpp" // for DRAW_MOUSE_CURSOR
#include "ConfigPanel.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Form/DataField/Enum.hpp"
#include "Hardware/DisplayBrightness.hpp"
#include "Hardware/RotateDisplay.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "LogFile.hpp"
#include "MainWindow.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Asset.hpp"
#include "UtilsSettings.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "util/StringFormat.hpp"
#include "util/Macros.hpp"
#include "util/StaticString.hxx"

#ifdef ANDROID
#include "Android/Main.hpp"
#include "Android/NativeView.hpp"
#endif

#ifdef USE_POLL_EVENT
#include "ui/event/Globals.hpp"
#include "ui/event/Queue.hpp"
#endif

#include <memory>

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

static constexpr StaticEnumChoice dark_mode_list[] = {
  { UISettings::DarkMode::AUTO, N_("Auto"),
    N_("Use the system-wide setting") },
  { UISettings::DarkMode::OFF, N_("Off"),
    N_("Black text on white background") },
  { UISettings::DarkMode::ON, N_("On"),
    N_("White text on black background") },
  nullptr
};

static void
FillDpiChoices(DataFieldEnum &df, unsigned value) noexcept
{
  static constexpr unsigned dpi_choices[] = {
    120, 160, 240, 260, 280, 300, 340, 360, 400, 420, 520,
  };

  df.AddChoice(0, _("Automatic"));
  for (unsigned dpi : dpi_choices) {
    StaticString<20> buffer;
    buffer.Format(_("%u dpi"), dpi);
    df.AddChoice(dpi, buffer);
  }
  df.SetValue(value);
}

std::unique_ptr<Widget>
CreateDisplayConfigPanel()
{
  const UISettings &ui_settings = CommonInterface::GetUISettings();

  struct Fields {
    DisplayType display_type;
    unsigned custom_dpi;
    DisplayOrientation orientation;
    UISettings::DarkMode dark_mode;
    int scale;
#ifdef HAVE_DISPLAY_BRIGHTNESS_CONTROL
    std::unique_ptr<DisplayBrightness> brightness;
    int brightness_percent;
#endif
#ifdef ANDROID
    bool full_screen;
#endif
#ifdef DRAW_MOUSE_CURSOR
    int cursor_size;
    bool invert_cursor;
#endif
  };

  auto fields = std::make_shared<Fields>();
  fields->display_type = ui_settings.display.display_type;
  fields->custom_dpi = ui_settings.custom_dpi;
  fields->orientation = ui_settings.display.orientation;
  fields->dark_mode = ui_settings.dark_mode;
  fields->scale = static_cast<int>(ui_settings.scale);
#ifdef HAVE_DISPLAY_BRIGHTNESS_CONTROL
  fields->brightness = DisplayBrightness::Detect();
  fields->brightness_percent = fields->brightness == nullptr ? 0
    : static_cast<int>(fields->brightness->GetBrightnessPercent());
#endif
#ifdef ANDROID
  fields->full_screen = ui_settings.display.full_screen;
#endif
#ifdef DRAW_MOUSE_CURSOR
  fields->cursor_size = ui_settings.display.cursor_size;
  fields->invert_cursor = ui_settings.display.invert_cursor_colors;
#endif
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddEnum(C_("Setting", "Display type"),
                _("Select the display technology. E-ink modes disable kinetic "
                  "and smooth scrolling for slow refresh screens."),
                display_type_list, fields->display_type, true);

  const char *dpi_help =
    _("The display resolution is used to adapt line widths, "
      "font size, landable size and more.");
  GroupedListWidget::ItemOptions dpi_options;
  dpi_options.help = dpi_help;
  dpi_options.expert = true;
  dpi_options.value_callback =
    [fields](GroupedListWidget::ValueState &state) {
      char buffer[32];
      state.text = fields->custom_dpi == 0 ? _("Automatic")
        : (StringFormat(buffer, sizeof(buffer), _("%u dpi"),
                        fields->custom_dpi), buffer);
    };
  list->AddValue(_("Display resolution"),
                 [list = list.get(), fields, dpi_help] {
    DataFieldEnum df;
    FillDpiChoices(df, fields->custom_dpi);
    if (!ComboPicker(_("Display resolution"), df, dpi_help) ||
        df.GetValue() == fields->custom_dpi)
      return;
    fields->custom_dpi = df.GetValue();
    list->UpdateValues();
  }, dpi_options);

  if (Display::RotateSupported())
    list->AddEnum(_("Display orientation"),
                  _("Rotate the display on devices that support it."),
                  display_orientation_list, fields->orientation);

#ifdef HAVE_DISPLAY_BRIGHTNESS_CONTROL
  if (fields->brightness != nullptr && fields->brightness->IsWritable())
    list->AddInteger(_("Screen brightness"),
                     _("Adjust the screen brightness."),
                     "%d %%", "%d", 0, 100, 5,
                     fields->brightness_percent);
  else if (fields->brightness != nullptr)
    list->AddValue(_("Screen brightness"),
                   _("Screen brightness is read-only because writing requires additional permissions."),
                   [fields](GroupedListWidget::ValueState &state) {
      char buffer[16];
      state.text = (StringFormat(buffer, sizeof(buffer), "%d %%",
                                 fields->brightness_percent), buffer);
    });
#endif

#ifdef ANDROID
  list->AddSwitch(_("Full screen"),
                  _("Run XCSoar in full screen mode"),
                  fields->full_screen);
#endif

  list->AddEnum(_("Dark mode"), nullptr, dark_mode_list,
                fields->dark_mode);
  list->AddInteger(_("Text size"), nullptr, "%d %%", "%d",
                   static_cast<int>(UISettings::SCALE_MIN),
                   static_cast<int>(UISettings::SCALE_MAX),
                   static_cast<int>(UISettings::SCALE_STEP),
                   fields->scale);

#ifdef DRAW_MOUSE_CURSOR
  list->AddInteger(_("Cursor zoom"), _("Cursor zoom factor"),
                   "%d x", "%d x", 1, 10, 1, fields->cursor_size);
  list->AddSwitch(_("Invert cursor color"), _("Enable black cursor"),
                  fields->invert_cursor);
#endif

  list->SetSaveCallback([fields](bool &changed) {
    UISettings &ui_settings = CommonInterface::SetUISettings();
    if (ConfigPanel::CommitSetting(
          changed, ui_settings.display.display_type, fields->display_type,
          ProfileKeys::DisplayType))
      SetDisplayType(ui_settings.display.display_type);
    if (ConfigPanel::CommitSetting(
          changed, ui_settings.custom_dpi, fields->custom_dpi,
          ProfileKeys::CustomDPI))
      require_restart = true;
    if (Display::RotateSupported() &&
        ConfigPanel::CommitSetting(
          changed, ui_settings.display.orientation, fields->orientation,
          ProfileKeys::MapOrientation)) {
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
    if (fields->brightness != nullptr && fields->brightness->IsWritable() &&
        static_cast<unsigned>(fields->brightness_percent) !=
        fields->brightness->GetBrightnessPercent())
      fields->brightness->SetBrightnessPercent(
        static_cast<unsigned>(fields->brightness_percent));
#endif
#ifdef ANDROID
    changed |= ConfigPanel::CommitSetting(
      changed, ui_settings.display.full_screen, fields->full_screen,
      ProfileKeys::FullScreen);
    native_view->SetFullScreen(Java::GetEnv(),
                               ui_settings.display.full_screen);
#endif
    changed |= ConfigPanel::CommitSetting(
      changed, ui_settings.dark_mode, fields->dark_mode,
      ProfileKeys::DarkMode);
    if (ConfigPanel::CommitSetting(
          changed, ui_settings.scale, static_cast<unsigned>(fields->scale),
          ProfileKeys::UIScale))
      require_restart = true;
#ifdef DRAW_MOUSE_CURSOR
    changed |= ConfigPanel::CommitSetting(
      changed, ui_settings.display.cursor_size,
      static_cast<uint8_t>(fields->cursor_size), ProfileKeys::CursorSize);
    CommonInterface::main_window->SetCursorSize(
      ui_settings.display.cursor_size);
    changed |= ConfigPanel::CommitSetting(
      changed, ui_settings.display.invert_cursor_colors, fields->invert_cursor,
      ProfileKeys::CursorColorsInverted);
    CommonInterface::main_window->SetCursorColorsInverted(
      ui_settings.display.invert_cursor_colors);
#endif
    return true;
  });

  return list;
}
