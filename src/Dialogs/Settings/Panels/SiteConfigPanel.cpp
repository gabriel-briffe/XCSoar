// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SiteConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Form/DataField/File.hpp"
#include "Form/DataField/MultiFile.hpp"
#include "Form/DataField/String.hpp"
#include "Language/Language.hpp"
#include "LocalPath.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Repository/FileType.hpp"
#include "Repository/Glue.hpp"
#include "UIGlobals.hpp"
#include "UtilsSettings.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "system/Path.hpp"
#include "util/StringCompare.hxx"

#include <memory>
#include <string>

static void
LoadFile(FileDataField &df, std::string_view key, FileType type) noexcept
{
  df.SetFileType(type);
  df.ScanMultiplePatterns(GetFileTypePatterns(type));
  if (const auto path = Profile::GetPath(key); path != nullptr)
    df.SetValue(path);
}

static void
LoadFiles(MultiFileDataField &df, std::string_view key, FileType type) noexcept
{
  const char *filters = GetFileTypePatterns(type);
  df.SetFileType(type);
  df.ScanMultiplePatterns(filters);
  for (const auto &path : Profile::GetMultiplePaths(key, filters))
    df.AddInitialPath(path);
}

template<typename DataField>
static void
AddFileRow(GroupedListWidget &list, const char *caption, const char *help,
           DataField &df, bool expert) noexcept
{
  GroupedListWidget::ItemOptions options;
  options.help = help;
  options.expert = expert;
  options.value_callback = [&df](GroupedListWidget::ValueState &state) {
    state.text = df.GetAsDisplayString();
  };
  list.AddValue(caption, [caption, help, &df, page = &list] {
    if (EditDataFieldDialog(caption, df, help))
      page->UpdateValues();
  }, std::move(options));
}

static bool
SaveFile(const FileDataField &df, std::string_view key) noexcept
{
  Path path = df.GetValue();
  if (const auto contracted = ContractLocalPath(path); contracted != nullptr)
    path = contracted;
  if (StringIsEqual(Profile::Get(key, ""), path.c_str()))
    return false;

  Profile::Set(key, path.c_str());
  return true;
}

static bool
SaveFiles(const MultiFileDataField &df, std::string_view key) noexcept
{
  std::string joined;
  for (const auto &value : df.GetPathFiles()) {
    const auto contracted = ContractLocalPath(value);
    const Path path = contracted != nullptr ? Path{contracted} : value;
    if (path.empty())
      continue;
    if (!joined.empty())
      joined += '|';
    joined += path.c_str();
  }

  const std::string old_value = Profile::Get(key, "");
  if (old_value == joined)
    return false;
  Profile::Set(key, joined.c_str());
  return true;
}
std::unique_ptr<Widget>
CreateSiteConfigPanel()
{
  struct Fields {
    FileDataField map;
    MultiFileDataField waypoints, watched, airfields, airspace;
    FileDataField flarm, rasp, checklist;
    std::string repositories;
  };

  auto fields = std::make_shared<Fields>();
  LoadFile(fields->map, ProfileKeys::MapFile, FileType::MAP);
  LoadFiles(fields->waypoints, ProfileKeys::WaypointFileList, FileType::WAYPOINT);
  LoadFiles(fields->watched, ProfileKeys::WatchedWaypointFileList, FileType::WAYPOINT);
  LoadFiles(fields->airfields, ProfileKeys::AirfieldFileList, FileType::WAYPOINTDETAILS);
  LoadFiles(fields->airspace, ProfileKeys::AirspaceFileList, FileType::AIRSPACE);
  LoadFile(fields->flarm, ProfileKeys::FlarmFile, FileType::FLARMNET);
  LoadFile(fields->rasp, ProfileKeys::RaspFile, FileType::RASP);
  LoadFile(fields->checklist, ProfileKeys::ChecklistFile, FileType::CHECKLIST);
  fields->repositories = Profile::Get(ProfileKeys::UserRepositoriesList, "");

  auto list = std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  list->AddGroup(nullptr);
  list->AddValue(_("XCSoar data path"), _("Click to view full path"),
                 [](GroupedListWidget::ValueState &state) {
                   state.text = GetPrimaryDataPath().c_str();
                 });
  AddFileRow(*list, _("Map database"), _("The name of the file (.xcm) containing terrain, topography, and optionally " "waypoints, their details and airspaces."), fields->map, false);
  AddFileRow(*list, _("Waypoints"), _("Primary waypoints files.  Supported file types are " "Cambridge/WinPilot files (.dat), " "Zander files (.wpz) or SeeYou files (.cup, .cupx)."), fields->waypoints, false);
  AddFileRow(*list, _("Watched WPTs"), _("Waypoint files containing special waypoints for which " "additional computations like " "calculation of arrival height in map display always " "takes place. Useful for " "waypoints like known reliable thermal sources (e.g. " "powerplants) or mountain passes."), fields->watched, true);
  AddFileRow(*list, _("WPT A/F details"), _("The files may contain extracts from enroute supplements " "or other contributed " "information about individual waypoints and airfields."), fields->airfields, true);
  AddFileRow(*list, _("Airspace"), _("List of active airspace files. Use the Add and Remove " "buttons to activate or deactivate" " airspace files respectively. Supported file types are: " "Openair (.openair /.txt /.air), and Tim Newport-Pearce (.sua)."), fields->airspace, false);
  AddFileRow(*list, _("FLARM database"), _("The name of the file containing information about registered FLARM devices."), fields->flarm, false);
  AddFileRow(*list, "RASP", _("Regional Atmospheric Soaring Prediction file providing " "weather forecasts for soaring. Displays color-coded map " "overlays for thermal strength, boundary layer winds, " "cloud cover, and other soaring-relevant parameters at " "various forecast times throughout the day."), fields->rasp, false);
  AddFileRow(*list, _("Checklist"), _("The checklist file containing pre-flight and other checklists."), fields->checklist, false);

  const char *repo_help =
    _("List of additional user repository URIs, separated by '|' character.");
  GroupedListWidget::ItemOptions repositories;
  repositories.help = repo_help;
  repositories.expert = true;
  repositories.value_callback = [fields](GroupedListWidget::ValueState &state) {
    state.text = fields->repositories;
  };
  list->AddValue(_("User repositories"), [fields, page = list.get(), repo_help] {
    DataFieldString df(fields->repositories.c_str());
    if (!EditDataFieldDialog(_("User repositories"), df, repo_help))
      return;
    fields->repositories = df.GetValue();
    page->UpdateValues();
  }, std::move(repositories));

  list->SetSaveCallback([fields](bool &changed) {
    MapFileChanged = SaveFile(fields->map, ProfileKeys::MapFile);
    WaypointFileChanged |= SaveFiles(fields->waypoints, ProfileKeys::WaypointFileList);
    WaypointFileChanged |= SaveFiles(fields->watched, ProfileKeys::WatchedWaypointFileList);
    AirspaceFileChanged |= SaveFiles(fields->airspace, ProfileKeys::AirspaceFileList);
    FlarmFileChanged = SaveFile(fields->flarm, ProfileKeys::FlarmFile);
    AirfieldFileChanged = SaveFiles(fields->airfields, ProfileKeys::AirfieldFileList);
    RaspFileChanged = SaveFile(fields->rasp, ProfileKeys::RaspFile);
    ChecklistFileChanged = SaveFile(fields->checklist, ProfileKeys::ChecklistFile);

    const std::string old_repos{Profile::Get(ProfileKeys::UserRepositoriesList, "")};
    if (old_repos != fields->repositories) {
      Profile::Set(ProfileKeys::UserRepositoriesList, fields->repositories.c_str());
      PurgeChangedUserRepositoryFiles(old_repos.c_str(), fields->repositories.c_str());
      UserRepositoriesListChanged = true;
    } else
      UserRepositoriesListChanged = false;

    changed |= WaypointFileChanged || AirfieldFileChanged ||
      AirspaceFileChanged || MapFileChanged || FlarmFileChanged ||
      RaspFileChanged || ChecklistFileChanged || UserRepositoriesListChanged;
    return true;
  });
  return list;
}
