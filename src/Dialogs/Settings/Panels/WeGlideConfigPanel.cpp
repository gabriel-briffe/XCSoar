// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WeGlideConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/Date.hpp"
#include "Formatter/TimeFormatter.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "net/client/WeGlide/Settings.hpp"
#include "time/BrokenDate.hpp"

#include <memory>

static void
AddToggle(GroupedListWidget &list, GroupedListWidget *page,
          const char *caption, const char *help, bool &field) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.toggle = true;
  options.checked = field;
  options.help = help;
  list.AddItem(caption, [&field, page] {
    field = !field;
    page->UpdateValues();
  }, options);
}

std::unique_ptr<Widget>
CreateWeGlideConfigPanel() noexcept
{
  const WeGlideSettings &weglide =
    CommonInterface::GetComputerSettings().weglide;

  struct Fields {
    bool enabled;
    bool automatic_upload;
    int pilot_id;
    BrokenDate pilot_birthdate;
  };

  auto fields = std::make_shared<Fields>(Fields{
    weglide.enabled,
    weglide.automatic_upload,
    static_cast<int>(weglide.pilot_id),
    weglide.pilot_birthdate,
  });

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();
  const auto shown = [fields] { return fields->enabled; };

  list->AddGroup(nullptr);
  AddToggle(*list, page, _("Enable"),
            _("Allow download of declared tasks from Weglide in the Task Manager."),
            fields->enabled);
  list->AddSwitch(_("Automatic Upload"),
                  _("Asks whether to upload flight to Weglide, after flight is "
                    "downloaded from external logger."),
                  fields->automatic_upload, false, shown);
  list->AddInteger(_("Pilot"),
                   _("Take this from your WeGlide Profile. Or set to 0 if not used."),
                   "%d", "%d", 1, 99999, 1, fields->pilot_id,
                   false, shown);

  list->AddValue(_("Pilot date of birth"), nullptr,
                 [fields](GroupedListWidget::ValueState &state) {
                   DataFieldDate df(fields->pilot_birthdate, nullptr);
                   state.text = df.GetAsDisplayString();
                   state.hidden = !fields->enabled;
                 },
                 [fields, page] {
                   DataFieldDate df(fields->pilot_birthdate, nullptr);
                   if (!EditDataFieldDialog(_("Pilot date of birth"), df,
                                            nullptr))
                     return;

                   fields->pilot_birthdate = df.GetValue();
                   page->UpdateValues();
                 });

  list->SetSaveCallback([fields](bool &changed) {
    auto &weglide = CommonInterface::SetComputerSettings().weglide;

    ConfigPanel::CommitSetting(changed, weglide.automatic_upload,
                               fields->automatic_upload,
                               ProfileKeys::WeGlideAutomaticUpload);

    if (fields->pilot_id >= 0) {
      const auto pilot_id = static_cast<uint32_t>(fields->pilot_id);
      ConfigPanel::CommitSetting(changed, weglide.pilot_id, pilot_id,
                                 ProfileKeys::WeGlidePilotID);
    }

    if (fields->pilot_birthdate.IsPlausible() &&
        fields->pilot_birthdate != weglide.pilot_birthdate) {
      char buffer[0x10];
      FormatISO8601(buffer, fields->pilot_birthdate);
      weglide.pilot_birthdate = fields->pilot_birthdate;
      Profile::Set(ProfileKeys::WeGlidePilotBirthDate, buffer);
      changed = true;
    }

    ConfigPanel::CommitSetting(changed, weglide.enabled, fields->enabled,
                               ProfileKeys::WeGlideEnabled);
    return true;
  });

  return list;
}
