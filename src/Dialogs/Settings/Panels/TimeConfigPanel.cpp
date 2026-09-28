// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TimeConfigPanel.hpp"
#include "Computer/Settings.hpp"
#include "ConfigPanel.hpp"
#include "Dialogs/DataField.hpp"
#include "Dialogs/DialogSettings.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Time.hpp"
#include "Formatter/LocalTimeFormatter.hpp"
#include "Formatter/TimeFormatter.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/ComputerProfile.hpp"
#include "Profile/Current.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "UIGlobals.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "time/BrokenDateTime.hpp"
#include "time/SystemTimeZone.hpp"
#include "time/TimeZones.hpp"
#include "ui/event/PeriodicTimer.hpp"
#include "util/StaticString.hxx"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>

using namespace std::chrono;

static constexpr auto UTC_OFFSET_STEP = minutes{15};

static constexpr StaticEnumChoice local_time_source_list[] = {
#ifndef KOBO
  /* the Kobo has no time zone configuration which we could follow */
  { LocalTimeSource::AUTOMATIC, N_("Automatic"),
    N_("Use the time zone which is configured in the operating system, and "
       "keep following it across daylight saving time changes and when "
       "travelling to another time zone.") },
#endif
  { LocalTimeSource::TIME_ZONE, N_("Time zone"),
    N_("Use the time zone selected below.  XCSoar knows its daylight saving "
       "time rules and applies the transitions on its own, which means the "
       "setting does not need to be corrected twice a year.") },
  nullptr
};

/**
 * The manual offset is what the other two sources exist to avoid, so it is
 * offered to experts only; a profile which uses it must of course still
 * be able to show and keep it.
 */
static constexpr StaticEnumChoice manual_utc_offset_list[] = {
  { LocalTimeSource::MANUAL_UTC_OFFSET, N_("Manual UTC offset"),
    N_("Use the fixed UTC offset entered below.  It has to be corrected "
       "manually whenever daylight saving time begins or ends.") },
  nullptr
};

struct TimeFields {
  LocalTimeSource source;
  StaticString<64> time_zone;
  RoughTimeDelta manual_utc_offset;
  bool manual_utc_offset_modified = false;
  bool set_system_time_from_gps;
};

static bool
OfferManualOffset(LocalTimeSource source) noexcept
{
  return UIGlobals::GetDialogSettings().expert ||
    source == LocalTimeSource::MANUAL_UTC_OFFSET;
}

static const char *
SourceLabel(LocalTimeSource source) noexcept
{
  for (const StaticEnumChoice *list : {local_time_source_list,
                                       manual_utc_offset_list}) {
    for (auto i = list; i->display_string != nullptr; ++i)
      if (i->id == static_cast<unsigned>(source))
        return gettext(i->display_string);
  }

  return "";
}

static void
FillLocalTimeSource(DataFieldEnum &df, LocalTimeSource source) noexcept
{
  df.EnableItemHelp(true);
  df.AddChoices(local_time_source_list);
  if (OfferManualOffset(source))
    df.AddChoices(manual_utc_offset_list);

  df.SetValue(source);
}

static void
FillTimeZone(DataFieldEnum &df, const char *id) noexcept
{
  for (const auto &i : GetTimeZones())
    df.addEnumText(i.id);

  if (!df.SetValue(id))
    df.SetValue("UTC");
}

static RoughTimeDelta
UTCOffset(const TimeFields &fields) noexcept
{
  switch (fields.source) {
  case LocalTimeSource::AUTOMATIC:
    return RoughTimeDelta::FromSeconds(GetCurrentTimeZoneOffset());

  case LocalTimeSource::TIME_ZONE:
    if (const auto offset = FindTimeZoneOffset(fields.time_zone.c_str(),
                                               system_clock::now()))
      return RoughTimeDelta::FromSeconds(offset->count());

    return RoughTimeDelta::FromSeconds(0);

  case LocalTimeSource::MANUAL_UTC_OFFSET:
    break;
  }

  return fields.manual_utc_offset;
}

static std::string
LocalTimeText(const TimeFields &fields) noexcept
{
  const NMEAInfo &basic = CommonInterface::Basic();

  /* without a GPS fix, the blackboard holds no time at all, and the
     preview would show the UTC offset instead of a time of day */
  const auto time = basic.time_available
    ? basic.time
    : TimeStamp{BrokenDateTime::NowUTC().DurationSinceMidnight()};

  const auto utc_offset = UTCOffset(fields);
  const int offset_seconds = utc_offset.AsSeconds();
  StaticString<32> buffer;
  buffer.Format("%s (UTC%c%s)",
                FormatLocalTimeHHMM(time, utc_offset).c_str(),
                offset_seconds < 0 ? '-' : '+',
                FormatSignedTimeHHMM(std::chrono::seconds{
                  std::abs(offset_seconds)}).c_str());
  return std::string{buffer.c_str()};
}

std::unique_ptr<Widget>
CreateTimeConfigPanel()
{
  const ComputerSettings &settings_computer =
    CommonInterface::GetComputerSettings();

  auto fields = std::make_shared<TimeFields>();
  fields->manual_utc_offset = settings_computer.utc_offset;
  Profile::LoadUTCOffset(Profile::map, fields->manual_utc_offset);
  fields->source = settings_computer.local_time_source;
  fields->time_zone = settings_computer.time_zone;
  fields->set_system_time_from_gps =
    settings_computer.set_system_time_from_gps;

#ifdef KOBO
  if (fields->source == LocalTimeSource::AUTOMATIC)
    /* the profile was written on another platform */
    fields->source = LocalTimeSource::TIME_ZONE;
#endif

  auto list =
    std::make_unique<GroupedListWidget>(UIGlobals::GetDialogLook());
  GroupedListWidget *page = list.get();
  list->AddGroup(nullptr);
  list->AddValue(_("Local time source"),
                 _("Selects where XCSoar gets the offset between "
                   "UTC and local time from."),
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = SourceLabel(fields->source);
                 },
                 [fields, page] {
                   DataFieldEnum df;
                   FillLocalTimeSource(df, fields->source);
                   if (!EditDataFieldDialog(_("Local time source"), df,
                                            _("Selects where XCSoar gets the offset between "
                                              "UTC and local time from.")))
                     return;

                   fields->source = static_cast<LocalTimeSource>(df.GetValue());
                   page->UpdateValues();
                 });
  list->AddValue(_("Time zone"),
                 _("The time zone of the airfield you are flying "
                   "at."),
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = fields->time_zone.c_str();
                   state.hidden = fields->source != LocalTimeSource::TIME_ZONE;
                 },
                 [fields, page] {
                   DataFieldEnum df;
                   FillTimeZone(df, fields->time_zone.c_str());
                   if (!EditDataFieldDialog(_("Time zone"), df,
                                            _("The time zone of the airfield you are flying "
                                              "at.")))
                     return;

                   fields->time_zone = df.GetAsString();
                   page->UpdateValues();
                 });
  list->AddValue(_("Manual UTC offset"),
                 _("The UTC offset field allows the UTC local time offset to be specified. It keeps "
                   "the value you entered even while another local time source is selected. The "
                   "local time is displayed below, along with the UTC offset which is currently in "
                   "effect."),
                 [fields](GroupedListWidget::ValueState &state) {
                   DataFieldTime df(Profile::MIN_UTC_OFFSET,
                                    Profile::MAX_UTC_OFFSET,
                                    seconds{fields->manual_utc_offset.AsSeconds()},
                                    UTC_OFFSET_STEP, nullptr);
                   df.SetMaxTokenNumber(2);
                   state.text = df.GetAsDisplayString();
                   state.hidden =
                     fields->source != LocalTimeSource::MANUAL_UTC_OFFSET;
                 },
                 [fields, page] {
                   DataFieldTime df(Profile::MIN_UTC_OFFSET,
                                    Profile::MAX_UTC_OFFSET,
                                    seconds{fields->manual_utc_offset.AsSeconds()},
                                    UTC_OFFSET_STEP, nullptr);
                   df.SetMaxTokenNumber(2);
                   if (!EditDataFieldDialog(_("Manual UTC offset"), df,
                                            _("The UTC offset field allows the UTC local time offset to be specified. It keeps "
                                              "the value you entered even while another local time source is selected. The "
                                              "local time is displayed below, along with the UTC offset which is currently in "
                                              "effect.")))
                     return;

                   fields->manual_utc_offset =
                     RoughTimeDelta::FromDuration(df.GetValue());
                   fields->manual_utc_offset_modified = true;
                   page->UpdateValues();
                 });
  list->AddValue(_("Local time"), nullptr,
                 [fields](GroupedListWidget::ValueState &state) {
                   state.text = LocalTimeText(*fields);
                 });
  list->AddSwitch(_("Use GPS time"),
                  _("If enabled sets the clock of the computer to the GPS time once a fix "
                    "is set. This is only necessary if your computer does not have a "
                    "real-time clock with battery backup or your computer frequently runs "
                    "out of battery power or otherwise loses time."),
                  fields->set_system_time_from_gps, true);

  struct Clock {
    UI::PeriodicTimer timer;
    bool running = false;

    explicit Clock(GroupedListWidget *page) noexcept
      :timer([page] { page->UpdateValues(); }) {}
  };

  auto clock = std::make_shared<Clock>(page);
  list->SetVisibilityCallback([clock, page](bool visible) {
    if (visible) {
      page->UpdateValues();
      if (!clock->running) {
        clock->timer.Schedule(seconds{1});
        clock->running = true;
      }
    } else if (clock->running) {
      clock->timer.Cancel();
      clock->running = false;
    }
  });

  list->SetSaveCallback([fields](bool &changed) {
    ComputerSettings &settings_computer =
      CommonInterface::SetComputerSettings();

    ConfigPanel::CommitSetting(changed, settings_computer.local_time_source,
                               fields->source);
    if (settings_computer.time_zone != fields->time_zone) {
      settings_computer.time_zone = fields->time_zone;
      Profile::Set(ProfileKeys::TimeZone,
                   settings_computer.time_zone.c_str());
      changed = true;
    }

    /* the source is written even if it did not change: without this key, a
       stored UTC offset means "manual" to Profile::Load(), because that
       is what it meant in older versions */
    Profile::SetEnum(ProfileKeys::LocalTimeSource,
                     settings_computer.local_time_source);

    if (settings_computer.local_time_source ==
        LocalTimeSource::MANUAL_UTC_OFFSET) {
      if (fields->manual_utc_offset != settings_computer.utc_offset) {
        settings_computer.utc_offset = fields->manual_utc_offset;
        changed = true;
      }
    } else {
      /* with the automatic sources, the UTC offset is owned by
         UTCOffsetProcessTimer(); apply it right away instead of the
         (disabled) form value, so the change is visible immediately */
      if (const auto new_utc_offset = settings_computer.GetCurrentUTCOffset();
          new_utc_offset != settings_computer.utc_offset) {
        settings_computer.utc_offset = new_utc_offset;
        changed = true;
      }
    }

    if (settings_computer.local_time_source ==
          LocalTimeSource::MANUAL_UTC_OFFSET ||
        fields->manual_utc_offset_modified) {
      /* remember the manual offset even while another source is active, so
         the user does not have to enter it again */
      Profile::Set(ProfileKeys::UTCOffsetSigned,
                   fields->manual_utc_offset.AsSeconds());
      fields->manual_utc_offset_modified = false;
      changed = true;
    }

    ConfigPanel::CommitSetting(changed,
                               settings_computer.set_system_time_from_gps,
                               fields->set_system_time_from_gps,
                               ProfileKeys::SetSystemTimeFromGPS);
    return true;
  });

  return list;
}
