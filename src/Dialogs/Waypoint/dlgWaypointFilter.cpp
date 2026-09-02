// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WaypointDialogs.hpp"
#include "Dialogs/Settings/PageSettingFilterListWidget.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Profile/Profile.hpp"
#include "Waypoints/WaypointsDisplaySetting.hpp"
#include "UIGlobals.hpp"
#include "Look/DialogLook.hpp"
#include "Language/Language.hpp"

void
dlgWaypointFilterShowModal() noexcept
{
  TWidgetDialog<PageSettingFilterListWidget>
    dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
           UIGlobals::GetDialogLook(),
           _("Waypoints"));
  dialog.AddButton(_("Close"), mrOK);
  dialog.SetWidget(WaypointsDisplaySetting::FilterDialogRowCount,
                   WaypointsDisplaySetting::FilterDialogRowId,
                   WaypointsDisplaySetting::GetLive,
                   PageSettingFilterList::PaintBoolDisplayColumn,
                   PageSettingFilterList::ActivateBoolDisplayToggle);

  dialog.ShowModal();

  if (dialog.GetWidget().IsModified())
    Profile::Save();
}
