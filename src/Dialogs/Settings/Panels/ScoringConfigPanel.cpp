// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "ScoringConfigPanel.hpp"
#include "ConfigPanel.hpp"
#include "Engine/Contest/Settings.hpp"
#include "Engine/Contest/Solvers/Contests.hpp"
#include "Form/DataField/Enum.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"

#include <memory>

static constexpr StaticEnumChoice fai_triangle_threshold_list[] = {
  { FAITriangleSettings::Threshold::FAI, "750km (FAI)" },
  { FAITriangleSettings::Threshold::KM500, "500km (OLC, DMSt)" },
  nullptr
};

std::unique_ptr<Widget>
CreateScoringConfigPanel()
{
  const ComputerSettings &settings_computer =
    CommonInterface::GetComputerSettings();
  const ContestSettings &contest_settings = settings_computer.contest;
  const MapSettings &map_settings = CommonInterface::GetMapSettings();

  struct Fields {
    Contest contest;
    bool predict;
    bool show_fai;
    FAITriangleSettings::Threshold fai_threshold;
    bool show_95;
  };

  auto fields = std::make_shared<Fields>(Fields{
    contest_settings.contest,
    contest_settings.predict,
    map_settings.show_fai_triangle_areas,
    map_settings.fai_triangle_settings.threshold,
    map_settings.show_95_percent_rule_helpers,
  });

  static const StaticEnumChoice contests_list[] = {
    { Contest::NONE, ContestToString(Contest::NONE),
      N_("Disable contest calculations") },
    { Contest::OLC_FAI, ContestToString(Contest::OLC_FAI),
      N_("Conforms to FAI triangle rules. Three turns and common start and finish. No leg less than 28% "
          "of total except for tasks longer than 500km: No leg less than 25% or larger than 45%.") },
    { Contest::OLC_CLASSIC, ContestToString(Contest::OLC_CLASSIC),
      N_("Up to seven points including start and finish, finish height must not be lower than "
          "start height less 1000 meters.") },
    { Contest::OLC_LEAGUE, ContestToString(Contest::OLC_LEAGUE),
      N_("The most recent contest with Sprint task rules.") },
    { Contest::OLC_PLUS, ContestToString(Contest::OLC_PLUS),
      N_("A combination of Classic and FAI rules. 30% of the FAI score are added to the Classic score.") },
    { Contest::DMST, ContestToString(Contest::DMST),
      /* German competition, no translation */
      "Deutsche Meisterschaft im Streckensegelflug." },
    { Contest::XCONTEST, ContestToString(Contest::XCONTEST),
      N_("PG online contest with different track values: Free flight - 1 km = 1.0 point; "
          "flat triangle - 1 km = 1.2 p; FAI triangle - 1 km = 1.4 p.") },
    { Contest::DHV_XC, ContestToString(Contest::DHV_XC),
      N_("European PG online contest of the DHV organization. Pretty much the same as the XContest rules, "
          "but with different track values: 1 km = 1.5 points, 1.75 p and 2.0 p for FAI triangles respectively.") },
    { Contest::SIS_AT, ContestToString(Contest::SIS_AT),
      N_("Austrian online glider contest. Tracks around max. six waypoints are scored. The "
          "bounding box part with 1 km = 1.0 point and the additional zick-zack part with 1 km = 0.5 p.") },
    { Contest::NET_COUPE, ContestToString(Contest::NET_COUPE),
      N_("FFVP Federal Cup (NetCoupe) on WeGlide. The scored path has at most "
          "three turnpoints between start and finish and at least 25 km total. "
          "Points are proportional to credited distance, 100 divided by the "
          "glider handicap (DAeC-style index), and a success factor of 1.0 for "
          "a free flight or 1.2 for a task declared electronically before "
          "takeoff. XCSoar live scoring uses a success factor of 1.0 only, not "
          "the 1.2 multiplier for declared tasks.") },
    { Contest::WEGLIDE_FREE, ContestToString(Contest::WEGLIDE_FREE),
      N_("WeGlide combines multiple scoring systems in the WeGlide Free contest. The free score is a combination "
          "of the free distance score and the area bonus. For the area bonus, the scoring program determines the "
          "largest FAI triangle and the largest Out & Return distance that can be fitted into the flight route.") },
    { Contest::WEGLIDE_OR, ContestToString(Contest::WEGLIDE_OR),
      N_("A start point, one turn point and a finish point are chosen from the flight path such that "
          "the distance between the start point and the turn point is maximized.") },
    { Contest::CHARRON, ContestToString(Contest::CHARRON),
      N_("LVZC Charron.online, 5 legs under 200km 6 legs above. Minimum leg distance is 20km, 5 points per km.") },
    nullptr
  };

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddEnum(_("Contest"),
      _("Select the rules used for calculating optimal points for a contest."),
      contests_list, fields->contest);
  list->AddSwitch(_("Predict Contest"),
                  _("If enabled, then the next task point is included in the "
                    "score calculation, assuming that you will reach it."),
                  fields->predict);

  list->AddSwitch(_("FAI triangle areas"),
                  _("Show FAI triangle areas on the map."),
                  fields->show_fai, true);

  list->AddEnum(_("FAI triangle threshold"),
                _("Specifies which threshold is used for \"large\" FAI triangles."),
                fai_triangle_threshold_list, fields->fai_threshold, true,
                [fields] { return fields->show_fai; });

  // xgettext:no-c-format
  list->AddSwitch(_("95% dist. rule helpers"),
                  _("Show helpers for Argentinean Federation \"95% distance\" rule. "
                    "The AAT Distance Around Target InfoBox will show projected "
                    "distance vs. maximum and change colors as you approach 95%."),
                  fields->show_95, true);

  list->SetSaveCallback([fields](bool &changed) {
    ContestSettings &contest_settings =
      CommonInterface::SetComputerSettings().contest;
    MapSettings &map_settings = CommonInterface::SetMapSettings();

    ConfigPanel::CommitSetting(changed, contest_settings.contest,
      fields->contest, ProfileKeys::OLCRules);
    ConfigPanel::CommitSetting(changed, contest_settings.predict,
      fields->predict, ProfileKeys::PredictContest);
    ConfigPanel::CommitSetting(changed,
      map_settings.show_fai_triangle_areas, fields->show_fai,
      ProfileKeys::ShowFAITriangleAreas);
    ConfigPanel::CommitSetting(changed,
      map_settings.fai_triangle_settings.threshold, fields->fai_threshold,
      ProfileKeys::FAITriangleThreshold);
    ConfigPanel::CommitSetting(changed,
      map_settings.show_95_percent_rule_helpers, fields->show_95,
      ProfileKeys::Show95PercentRuleHelpers);

    /* ContestEnumLayout=2 = current Contest encoding (see ContestProfile).
       Only stamp when OLCRules is present — do not add the key to
       untouched profiles.  Rewrite OLCRules so a migrated old NONE
       (stored as 14) is not read as NET_COUPE after the stamp. */
    unsigned contest_enum_layout = 0;
    if (Profile::Exists(ProfileKeys::OLCRules) &&
        (!Profile::Get(ProfileKeys::ContestEnumLayout,
                       contest_enum_layout) ||
         contest_enum_layout < 2U)) {
      Profile::Set(ProfileKeys::ContestEnumLayout, 2U);
      Profile::SetEnum(ProfileKeys::OLCRules, contest_settings.contest);
      changed = true;
    }

    return true;
  });

  return list;
}
