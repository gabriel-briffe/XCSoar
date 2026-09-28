// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InfoBoxesConfigPanel.hpp"
#include "../dlgConfigInfoboxes.hpp"
#include "ConfigPanel.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Look/Look.hpp"
#include "Profile/Current.hpp"
#include "Profile/InfoBoxConfig.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>
#include <string>
std::unique_ptr<Widget>
CreateInfoBoxesConfigPanel()
{
  const InfoBoxSettings &settings =
    CommonInterface::GetUISettings().info_boxes;

  struct Fields {
    bool use_final_glide;
  };

  auto fields = std::make_shared<Fields>(Fields{
    settings.use_final_glide,
  });
  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();
  list->AddGroup(nullptr);

  for (unsigned i = 0; i < InfoBoxSettings::MAX_PANELS; ++i) {
    const std::string initial{gettext(settings.panels[i].name)};

    GroupedListWidget::ItemOptions options;
    options.chevron = true;
    options.expert = i > 2;
    options.value_callback =
      [i, initial](GroupedListWidget::ValueState &state) {
        const char *name = gettext(CommonInterface::GetUISettings()
                                   .info_boxes.panels[i].name);
        if (initial != name)
          state.text = name;
      };

    list->AddItem(initial.c_str(), [i, page] {
      InfoBoxSettings &settings =
        CommonInterface::SetUISettings().info_boxes;
      InfoBoxSettings::Panel &data = settings.panels[i];

      const bool changed =
        dlgConfigInfoboxesShowModal(UIGlobals::GetMainWindow(),
                                    UIGlobals::GetDialogLook(),
                                    UIGlobals::GetLook().info_box,
                                    settings.geometry, data,
                                    i >= InfoBoxSettings::PREASSIGNED_PANELS);
      if (!changed)
        return;

      Profile::Save(Profile::map, data, i);
      Profile::Save();
      page->UpdateValues();
    }, options);
  }

  list->AddSwitch(_("Use final glide mode"),
                  _("Controls whether the \"final glide\" InfoBox mode should be used on \"auto\" pages."),
                  fields->use_final_glide);

  list->SetSaveCallback([fields](bool &changed) {
    InfoBoxSettings &settings = CommonInterface::SetUISettings().info_boxes;
    ConfigPanel::CommitSetting(changed, settings.use_final_glide,
                               fields->use_final_glide,
                               ProfileKeys::UseFinalGlideDisplayMode);
    return true;
  });

  return list;
}
