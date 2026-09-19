// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Dialogs/DataManagement/DataManagement.hpp"
#include "Dialogs/dlgConfigMenu.hpp"
#include "UIGlobals.hpp"

void
ShowDataManagementDialog()
{
  dlgConfigDataShowModal(UIGlobals::GetMainWindow());
}
