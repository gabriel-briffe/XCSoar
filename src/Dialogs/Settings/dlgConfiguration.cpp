// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Dialogs/Dialogs.h"
#include "Dialogs/InternalLink.hpp"
#include "Dialogs/Message.hpp"
#include "Dialogs/dlgConfigMenu.hpp"
#include "Widget/ArrowPagerWidget.hpp"
#include "Widget/CreateWindowWidget.hpp"
#include "Widget/Widget.hpp"  /* NullWidget */
#include "Dialogs/WidgetDialog.hpp"
#include "Look/DialogLook.hpp"
#include "UIGlobals.hpp"
#include "ui/event/KeyCode.hpp"
#include "Form/TabMenuDisplay.hpp"
#include "Form/TabMenuData.hpp"
#include "Form/CheckBox.hpp"
#include "Form/Button.hpp"
#include "Screen/Layout.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "ConfigMenuData.hpp"
#include "Panels/ConfigPanel.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "UtilsSettings.hpp"
#include "util/Macros.hpp"
#include "util/Compiler.h"

#include <cassert>

static unsigned current_page;

// TODO: eliminate global variables
static ArrowPagerWidget *pager;

static void
OnUserLevel(bool expert) noexcept;

class ConfigurationExtraButtons final
  : public NullWidget {
  struct Layout {
    PixelRect expert, button2, button1;

    Layout(const PixelRect &rc):expert(rc), button2(rc), button1(rc) {
      const unsigned height = rc.GetHeight();
      const unsigned max_control_height = ::Layout::GetMaximumControlHeight();

      if (height >= 3 * max_control_height) {
        expert.bottom = expert.top + max_control_height;

        button1.top = button2.bottom = rc.bottom - max_control_height;
        button2.top = button2.bottom - max_control_height;
      } else {
        expert.right = button2.left = unsigned(rc.left * 2 + rc.right) / 3;
        button2.right = button1.left = unsigned(rc.left + rc.right * 2) / 3;
      }
    }
  };

  const DialogLook &look;

  CheckBoxControl expert;
  Button button2, button1;
  bool borrowed2, borrowed1;

public:
  ConfigurationExtraButtons(const DialogLook &_look)
    :look(_look),
     borrowed2(false), borrowed1(false) {}

  Button &GetButton(unsigned number) {
    switch (number) {
    case 1:
      return button1;

    case 2:
      return button2;

    default:
      assert(false);
      gcc_unreachable();
    }
  }

protected:
  /* virtual methods from Widget */
  PixelSize GetMinimumSize() const noexcept override {
    return {
      CheckBoxControl::GetMinimumWidth(look,
                                       ::Layout::GetMaximumControlHeight(),
                                       _("Expert")),
      ::Layout::GetMaximumControlHeight() * 3,
    };
  }

  void Prepare(ContainerWindow &parent,
               const PixelRect &rc) noexcept override {
    Layout layout(rc);

    expert.CreateInDialogForm(parent, look, _("Expert"), layout.expert,
                              [](bool value){ OnUserLevel(value); });

    WindowStyle style;
    style.Hide();
    style.TabStop();

    button2.Create(parent, look.button, "", layout.button2, style);
    button1.Create(parent, look.button, "", layout.button1, style);
  }

  void Show(const PixelRect &rc) noexcept override {
    Layout layout(rc);

    expert.SetState(CommonInterface::GetUISettings().dialog.expert);
    expert.MoveAndShow(layout.expert);

    if (borrowed2)
      button2.MoveAndShow(layout.button2);
    else
      button2.Move(layout.button2);

    if (borrowed1)
      button1.MoveAndShow(layout.button1);
    else
      button1.Move(layout.button1);
  }

  void Hide() noexcept override {
    expert.FastHide();
    button2.FastHide();
    button1.FastHide();
  }

  void Move(const PixelRect &rc) noexcept override {
    Layout layout(rc);
    expert.Move(layout.expert);
    button2.Move(layout.button2);
    button1.Move(layout.button1);
  }
};

void
ConfigPanel::BorrowExtraButton(unsigned i, const char *caption,
                               std::function<void()> callback) noexcept
{
  if (pager == nullptr)
    return;

  ConfigurationExtraButtons &extra =
    (ConfigurationExtraButtons &)pager->GetExtra();
  Button &button = extra.GetButton(i);
  button.SetCaption(caption);
  button.SetCallback(std::move(callback));
  button.Show();
}

void
ConfigPanel::ReturnExtraButton(unsigned i)
{
  if (pager == nullptr)
    return;

  ConfigurationExtraButtons &extra =
    (ConfigurationExtraButtons &)pager->GetExtra();
  Button &button = extra.GetButton(i);
  button.Hide();
}

static void
OnUserLevel(bool expert) noexcept
{
  CommonInterface::SetUISettings().dialog.expert = expert;

  /* Keep Profile I/O out of this checkbox callback (pager is mid-
     relayout). Persist UserLevel when the dialog closes instead. */

  /* force layout update */
  pager->PagerWidget::Move(pager->GetPosition());
}

/**
 * Back on the menu page commits (mrOK).  On a settings page, return
 * to the menu.  Close (exit button) always commits.
 */
static void
OnCloseClicked(WidgetDialog &dialog)
{
  if (pager->GetCurrentIndex() == 0)
    dialog.SetModalResult(mrOK);
  else
    pager->ClickPage(0);
}

static void
OnPageFlipped(WidgetDialog &dialog, TabMenuDisplay &menu)
{
  menu.OnPageFlipped();

  char buffer[128];
  const char *caption = menu.GetCaption(buffer, ARRAY_SIZE(buffer));
  if (caption == nullptr)
    caption = _("Configuration");
  dialog.SetCaption(caption);
}

void dlgConfigurationShowModal()
{
  const DialogLook &look = UIGlobals::GetDialogLook();

  WidgetDialog dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
                      look, _("Configuration"));

  pager = new ArrowPagerWidget(look.button,
                               [&dialog](){ OnCloseClicked(dialog); },
                               std::make_unique<ConfigurationExtraButtons>(look),
                               [&dialog](){ dialog.SetModalResult(mrOK); });

  auto _menu = std::make_unique<TabMenuDisplay>(*pager, look);
  auto &menu = *_menu;
  pager->Add(std::make_unique<CreateWindowWidget>([&_menu](ContainerWindow &parent,
                                                           const PixelRect &rc,
                                                           WindowStyle style) {
    style.TabStop();
    _menu->Create(parent, rc, style);
    return std::move(_menu);
  }));

  menu.InitMenu(ConfigMenuData::list_groups,
                ConfigMenuData::list_group_count);

  /* restore last selected menu item */
  menu.SetCursor(current_page);

  pager->SetPageFlippedCallback([&dialog, &menu](){
    OnPageFlipped(dialog, menu);
  });

  dialog.FinishPreliminary(pager);

  /* Esc on a settings panel returns to the menu (same as Back);
     on the menu itself, leave Esc to cancel the dialog. */
  dialog.SetKeyDownFunction([&dialog](unsigned key_code) {
    if (key_code != KEY_ESCAPE || pager->GetCurrentIndex() == 0)
      return false;

    OnCloseClicked(dialog);
    return true;
  });

  const int result = dialog.ShowModal();

  /* save page number for next time this dialog is opened */
  current_page = menu.GetCursor();

  /* Persist Expert only on OK. Missing UserLevel means beginner —
     write "1" when enabling Expert; remove the key when returning to
     beginner (do not leave UserLevel=0 cruft) (#1793). */
  bool expert_changed = false;
  if (result == mrOK) {
    const bool expert = CommonInterface::GetUISettings().dialog.expert;
    if (expert) {
      bool profile_expert = false;
      Profile::Get(ProfileKeys::UserLevel, profile_expert);
      if (!profile_expert) {
        Profile::Set(ProfileKeys::UserLevel, true);
        expert_changed = true;
      }
    } else if (Profile::Exists(ProfileKeys::UserLevel)) {
      Profile::Remove(ProfileKeys::UserLevel);
      expert_changed = true;
    }
  }

  if (dialog.GetChanged() || expert_changed) {
    Profile::Save();
    if (require_restart)
      ShowMessageBox(_("Changes to configuration saved. Restart XCSoar to apply changes."),
                  "", MB_OK);
  }

  pager = nullptr;
}

void
ShowConfigPanel(const char *title,
                std::unique_ptr<Widget> (*create_panel)())
{
  const UISettings old_ui_settings = CommonInterface::GetUISettings();
  SettingsEnter();

  ArrowPagerWidget *const previous_pager = pager;

  try {
    const DialogLook &look = UIGlobals::GetDialogLook();
    WidgetDialog dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
                        look, title);

    /* Same host as dlgConfigurationShowModal so panels that call
       ConfigPanel::BorrowExtraButton (Filter, Colours, …) work.
       Back returns to the parent (tiled) menu; Close dismisses the
       whole stack back to the map. */
    pager = new ArrowPagerWidget(look.button,
                                 [&dialog](){ dialog.SetModalResult(mrOK); },
                                 std::make_unique<ConfigurationExtraButtons>(look),
                                 [&dialog](){
                                   RequestTiledMenuCloseAll();
                                   dialog.SetModalResult(mrOK);
                                 });
    pager->Add(create_panel());
    dialog.FinishPreliminary(pager);
    dialog.ShowModal();

    if (dialog.GetChanged())
      Profile::Save();
  } catch (...) {
    pager = previous_pager;
    SettingsLeave(old_ui_settings);
    throw;
  }

  pager = previous_pager;
  SettingsLeave(old_ui_settings);
}
