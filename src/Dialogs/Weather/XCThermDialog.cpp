// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "XCThermDialog.hpp"
#include "Dialogs/Error.hpp"
#include "Dialogs/Message.hpp"
#include "Dialogs/GroupedListPicker.hpp"
#include "Components.hpp"
#include "NetComponents.hpp"
#include "PageActions.hpp"
#include "Weather/Features.hpp"

#ifdef HAVE_HTTP

#include "Form/Button.hpp"
#include "Form/CheckBox.hpp"
#include "Form/ButtonPanel.hpp"
#include "Form/DataField/Enum.hpp"
#include "Look/DialogLook.hpp"
#include "Renderer/TwoTextRowsRenderer.hpp"
#include "Screen/Layout.hpp"
#include "UIGlobals.hpp"
#include "Widget/ButtonPanelWidget.hpp"
#include "Widget/GroupedListWidget.hpp"
#include "Widget/MultiSelectListWidget.hpp"
#include "Widget/TwoWidgets.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "ui/event/PeriodicTimer.hpp"
#include "util/StaticString.hxx"
#include "Weather/xctherm/XCThermAPI.hpp"
#include "Weather/xctherm/XCThermCatalog.hpp"
#include "Weather/xctherm/XCThermDownloadGlue.hpp"
#include "Weather/xctherm/XCThermDownloadJob.hpp"
#include "Weather/xctherm/XCThermMapOverlay.hpp"
#include "LogFile.hpp"
#include "lib/fmt/ToBuffer.hxx"
#include "net/http/Init.hpp"

#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

/**
 * Download metadata for a layer — second row of the checklist.
 * For a span download, sizes/speed are totals across all hourly slices.
 */
struct LayerDownloadInfo {
  enum Status { NONE, PENDING, DONE, FAILED, CANCELED };
  Status status = NONE;
  double wire_mb = 0.0;
  double speed_mbs = 0.0;
  unsigned span_hours = 0;
  unsigned future_hours = 0;
  unsigned new_downloads = 0;
  unsigned pending_index = 0;
  unsigned pending_total = 0;
  uint64_t pending_bytes_now = 0;
  uint64_t pending_bytes_total = 0;
  unsigned retry_attempt = 0;
  unsigned retry_seconds_left = 0;
  std::string download_time;
  std::string issued_utc;
};

static LayerDownloadInfo download_info_ch[
  XCTherm::GetRegion(XCTherm::Region::CH).layer_count];
static LayerDownloadInfo download_info_uk[
  XCTherm::GetRegion(XCTherm::Region::UK).layer_count];

static LayerDownloadInfo *
GetDownloadInfo(unsigned model) noexcept
{
  if (XCTherm::ToRegion(model) == XCTherm::Region::UK)
    return download_info_uk;
  return download_info_ch;
}

static void
ResetAllDownloadInfo() noexcept
{
  for (auto &info : download_info_ch)
    info = LayerDownloadInfo{};
  for (auto &info : download_info_uk)
    info = LayerDownloadInfo{};
}

static constexpr StaticEnumChoice span_list[] = {
  { 1, N_("1 hour") },
  { 3, N_("3 hours") },
  { 6, N_("6 hours") },
  { 12, N_("12 hours") },
  { 18, N_("18 hours") },
  nullptr
};

static bool
LayerParameterInList(std::string_view parameter,
                     std::string_view list) noexcept
{
  while (!list.empty()) {
    const auto comma = list.find(',');
    const std::string_view token = list.substr(0, comma);
    if (token == parameter)
      return true;
    if (comma == std::string_view::npos)
      break;
    list.remove_prefix(comma + 1);
  }
  return false;
}

static void
LoadSelectedLayersFromProfile(unsigned model,
                              MultiSelectListWidget &list) noexcept
{
  const auto &region = XCTherm::GetRegion(model);
  list.SetLengthWithSelection(region.layer_count);

  const char *configured =
    Profile::Get(ProfileKeys::XCThermSelectedLayers);
  if (configured != nullptr && *configured != '\0') {
    const std::string_view saved{configured};
    bool any = false;
    for (unsigned i = 0; i < region.layer_count; ++i) {
      if (!LayerParameterInList(region.layers[i].api_parameter, saved))
        continue;
      list.SetSelected(i, true);
      any = true;
    }
    if (any)
      return;
  }

  const int active = XCTherm::FindActiveLayerIndex(
    CommonInterface::GetComputerSettings().weather.xctherm);
  if (active >= 0)
    list.SetSelected(unsigned(active), true);
}

static void
SaveSelectedLayersToProfile(unsigned model,
                            const MultiSelectListWidget &list) noexcept
{
  const auto &region = XCTherm::GetRegion(model);
  StaticString<512> value;
  value.clear();
  bool first = true;
  for (unsigned i = 0; i < region.layer_count; ++i) {
    if (!list.IsSelected(i))
      continue;
    if (!first)
      value += ',';
    first = false;
    value += region.layers[i].api_parameter;
  }
  Profile::Set(ProfileKeys::XCThermSelectedLayers, value.c_str());
  Profile::Save();
}

StaticString<200>
FormatLayerStatus(unsigned model, unsigned layer_index,
                  unsigned span_hours_setting) noexcept
{
  StaticString<200> text;
  const auto &region = XCTherm::GetRegion(model);
  if (layer_index >= region.layer_count) {
    text = _("None");
    return text;
  }

  const auto *info = GetDownloadInfo(model);
  switch (info[layer_index].status) {
  case LayerDownloadInfo::PENDING: {
    const auto &p = info[layer_index];
    if (p.retry_seconds_left > 0)
      text.Format(_("Slot %u: reconnect in %us (try #%u)"),
                  p.pending_index,
                  p.retry_seconds_left,
                  p.retry_attempt + 1);
    else if (p.pending_bytes_total > 0) {
      const double now_mb = (double)p.pending_bytes_now / (1024.0 * 1024.0);
      const double tot_mb = (double)p.pending_bytes_total / (1024.0 * 1024.0);
      text.Format(_("Slot %u/%u: %.2f / %.2f MB"),
                  p.pending_index, p.pending_total, now_mb, tot_mb);
    } else if (p.pending_bytes_now > 0) {
      const double now_mb = (double)p.pending_bytes_now / (1024.0 * 1024.0);
      text.Format(_("Slot %u/%u: %.2f MB"),
                  p.pending_index, p.pending_total, now_mb);
    } else if (p.pending_total > 0)
      text.Format(_("Slot %u/%u: connecting..."),
                  p.pending_index, p.pending_total);
    else
      text = _("Connecting...");
    break;
  }
  case LayerDownloadInfo::DONE: {
    const unsigned future = info[layer_index].future_hours;
    if (info[layer_index].new_downloads == 0)
      text.Format(_("%u/%uh | Issued %s | %s"),
                  future, span_hours_setting,
                  info[layer_index].issued_utc.c_str(),
                  info[layer_index].download_time.c_str());
    else
      text.Format(_("%u/%uh (%u new) | Issued %s | %.2f MB wire "
                    "%.1f MB/s | %s"),
                  future, span_hours_setting,
                  info[layer_index].new_downloads,
                  info[layer_index].issued_utc.c_str(),
                  info[layer_index].wire_mb,
                  info[layer_index].speed_mbs,
                  info[layer_index].download_time.c_str());
    break;
  }
  case LayerDownloadInfo::FAILED:
    text = _("Download failed");
    break;
  case LayerDownloadInfo::CANCELED:
    text = C_("Status", "Cancelled");
    break;
  default:
    text = _("Not downloaded");
    break;
  }

  return text;
}

class XCThermLayerListWidget;

/**
 * Strip under the layer checklist: download span and clear-cache.
 * Time/altitude stay on the map weather controls.  Sized to its rows
 * so the checklist keeps the room above (SkySight).
 */
class XCThermOptionsPanel final : public GroupedListWidget {
  XCThermLayerListWidget *layer_list = nullptr;
  unsigned active_job_count = 0;

public:
  XCThermOptionsPanel() noexcept
    :GroupedListWidget(UIGlobals::GetDialogLook()) {}

  void SetLayerList(XCThermLayerListWidget &_list) noexcept {
    layer_list = &_list;
  }

  void SetActiveJobCount(unsigned count) noexcept {
    if (active_job_count == count)
      return;
    active_job_count = count;
    if (GetItemCount() > 0)
      UpdateValues();
  }

  void RefreshStatus() noexcept {
    if (GetItemCount() == 0)
      return;
    UpdateValues();
  }

  PixelSize GetMinimumSize() const noexcept override {
    const unsigned height = GetContentHeight();
    PixelSize size = GroupedListWidget::GetMinimumSize();
    if (height != 0)
      size.height = height;
    return size;
  }

  PixelSize GetMaximumSize() const noexcept override {
    return GetMinimumSize();
  }

  void Prepare(ContainerWindow &parent,
               const PixelRect &rc) noexcept override;

private:
  void SaveSpan() noexcept;
  void PickSpan() noexcept;
  void DeleteAllClicked() noexcept;
  void Fill() noexcept;
};

class XCThermLayerListWidget final : public MultiSelectListWidget {
  TwoTextRowsRenderer row_renderer;
  ButtonPanelWidget *buttons_widget = nullptr;
  Button *preload_button = nullptr;
  XCThermOptionsPanel *options_panel = nullptr;

  std::shared_ptr<XCThermDownloadJob> active_job;
  std::vector<unsigned> preload_queue;
  UI::PeriodicTimer poll_timer{[this]{ PollDownload(); }};
  bool loading_selection = false;

public:
  static XCThermLayerListWidget *active;

  XCThermLayerListWidget() noexcept = default;

  ~XCThermLayerListWidget() noexcept override {
    if (auto *glue = GetXCThermDownloadGlue())
      glue->Abandon();
    if (active == this)
      active = nullptr;
  }

  void SetButtonPanel(ButtonPanelWidget &_buttons) noexcept {
    buttons_widget = &_buttons;
  }

  void SetOptionsPanel(XCThermOptionsPanel &_options) noexcept {
    options_panel = &_options;
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  void Show(const PixelRect &rc) noexcept override;
  void Hide() noexcept override;
  void Unprepare() noexcept override;
  void InvalidateList() noexcept;

private:
  void CreateButtons(ButtonPanel &buttons) noexcept;
  void SyncButtons() noexcept;
  void SaveSelection() noexcept;
  void LoadSelection() noexcept;
  void RehydrateRowsFromCache() noexcept;
  void PreloadSelectedClicked() noexcept;
  void DownloadClicked() noexcept;
  void CancelDownload() noexcept;
  void StartDownload(unsigned layer_index) noexcept;
  void StartNextQueuedDownload() noexcept;
  void PollDownload() noexcept;
  void FinishDownload() noexcept;

  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;

protected:
  void OnSelectionChanged() noexcept override {
    if (loading_selection)
      return;
    SaveSelection();
    SyncButtons();
  }
};

XCThermLayerListWidget *XCThermLayerListWidget::active = nullptr;

void
XCThermOptionsPanel::SaveSpan() noexcept
{
  /* Span is session-only (not persisted); still keep model/parameter
     keys consistent with the config panel. */
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  Profile::Set(ProfileKeys::XCThermModel, (int)settings.model);
  Profile::Set(ProfileKeys::XCThermParameter, (int)settings.parameter);
  Profile::Set(ProfileKeys::XCThermWaveHeight, (int)settings.wave_height);
  Profile::Set(ProfileKeys::XCThermVerticalWindAGL,
               (int)settings.vertical_wind_agl);
}

void
XCThermOptionsPanel::PickSpan() noexcept
{
  if (active_job_count > 0)
    return;

  auto &settings = CommonInterface::SetComputerSettings().weather.xctherm;
  unsigned span = settings.download_span_hours;
  if (span == 0)
    span = 1;
  if (!PickEnum(C_("Weather control", "Span"),
                _("How many forecast hours to download for each selected "
                  "layer."),
                span_list, span))
    return;

  settings.download_span_hours = span;
  SaveSpan();
  UpdateValues();
  if (layer_list != nullptr)
    layer_list->InvalidateList();
}

void
XCThermOptionsPanel::DeleteAllClicked() noexcept
{
  if (active_job_count > 0)
    return;

  if (ShowMessageBox(_("Delete all cached XC Therm forecast data? "
                       "This includes every altitude layer and region."),
                     _("XC Therm"), MB_YESNO | MB_ICONWARNING) != IDYES)
    return;

  XCThermAPI::Instance().ClearAllCachedData();
  ResetAllDownloadInfo();
  XCTherm::ClearMapOverlay();

  if (layer_list != nullptr)
    layer_list->InvalidateList();

  PageActions::Update();
}

void
XCThermOptionsPanel::Fill() noexcept
{
  AddGroup();

  AddValue(C_("Weather control", "Span"),
           _("How many forecast hours to download for each selected layer."),
           [this](ValueState &state) {
             const auto &live =
               CommonInterface::GetComputerSettings().weather.xctherm;
             const unsigned span = live.download_span_hours == 0
               ? 1 : live.download_span_hours;
             state.text = GetEnumCaption(span_list, span);
             state.disabled = active_job_count > 0;
           },
           [this]{ PickSpan(); });

  AddButton(C_("Button", "Clear downloaded data"),
            [this]{ DeleteAllClicked(); });
}

void
XCThermOptionsPanel::Prepare(ContainerWindow &parent,
                             const PixelRect &rc) noexcept
{
  Fill();
  GroupedListWidget::Prepare(parent, rc);
}

void
XCThermLayerListWidget::CreateButtons(ButtonPanel &buttons) noexcept
{
  preload_button = buttons.Add(C_("Button", "Preload Selected"), [this]() {
    PreloadSelectedClicked();
  });
  buttons.Add(_("Stop"), [this]() {
    DownloadClicked();
  });
  buttons.EnableCursorSelection();
}

void
XCThermLayerListWidget::SyncButtons() noexcept
{
  if (preload_button == nullptr)
    return;

  const bool job_running = (bool)active_job;
  preload_button->SetEnabled(!job_running && GetSelectedCount() > 0);

  if (options_panel != nullptr)
    options_panel->SetActiveJobCount(job_running ? 1u : 0u);
}

void
XCThermLayerListWidget::SaveSelection() noexcept
{
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  SaveSelectedLayersToProfile(settings.model, *this);
}

void
XCThermLayerListWidget::LoadSelection() noexcept
{
  loading_selection = true;
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  LoadSelectedLayersFromProfile(settings.model, *this);
  loading_selection = false;
  SyncButtons();
}

void
XCThermLayerListWidget::InvalidateList() noexcept
{
  GetList().Invalidate();
  SyncButtons();
}

void
XCThermLayerListWidget::RehydrateRowsFromCache() noexcept
{
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  const auto &region = XCTherm::GetRegion(settings.model);
  auto *info = GetDownloadInfo(settings.model);
  auto &api = XCThermAPI::Instance();

  api.EnableDiskCache();

  for (unsigned i = 0; i < region.layer_count; ++i) {
    if (info[i].status != LayerDownloadInfo::NONE)
      continue;

    const auto summary =
      api.GetCachedLayerSummary(region.layers[i].api_parameter);
    if (summary.hours.empty())
      continue;

    LayerDownloadInfo &row = info[i];
    row.status = LayerDownloadInfo::DONE;
    row.span_hours = (unsigned)summary.hours.size();
    row.future_hours = summary.future_hours;
    row.new_downloads = 0;
    row.wire_mb = 0.0;
    row.speed_mbs = 0.0;
    row.pending_index = 0;
    row.pending_total = 0;
    row.pending_bytes_now = 0;
    row.pending_bytes_total = 0;
    row.retry_attempt = 0;
    row.retry_seconds_left = 0;

    if (summary.latest_run_date.size() == 8 &&
        summary.latest_run_hour.size() == 2) {
      const std::string &d = summary.latest_run_date;
      row.issued_utc = std::string(FmtBuffer<32>("{}-{}-{} {} UTC",
                                                 d.substr(0, 4),
                                                 d.substr(4, 2),
                                                 d.substr(6, 2),
                                                 summary.latest_run_hour).c_str());
    } else {
      row.issued_utc = "?";
    }

    if (summary.latest_downloaded_at > 0) {
      const std::time_t t = (std::time_t)summary.latest_downloaded_at;
      std::tm *lt = std::localtime(&t);
      char tbuf[16];
      if (lt && std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", lt) > 0)
        row.download_time = tbuf;
    }
  }
}

void
XCThermLayerListWidget::PreloadSelectedClicked() noexcept
{
  if (active_job)
    return;

  const auto selected = GetSelectedIndices();
  if (selected.empty()) {
    ShowMessageBox(_("No layers selected."), "XC Therm", MB_OK);
    return;
  }

  StaticString<256> prompt;
  prompt.Format(_("Download %u selected XC Therm layers for offline use?"),
                unsigned(selected.size()));
  if (ShowMessageBox(prompt.c_str(), _("XC Therm"), MB_YESNO | MB_ICONQUESTION)
      != IDYES)
    return;

  preload_queue = selected;
  StartNextQueuedDownload();
}

void
XCThermLayerListWidget::DownloadClicked() noexcept
{
  if (active_job)
    CancelDownload();
}

void
XCThermLayerListWidget::CancelDownload() noexcept
{
  if (!active_job)
    return;

  preload_queue.clear();

  if (auto *glue = GetXCThermDownloadGlue())
    glue->RequestCancel();
  else
    active_job->cancel.store(true);
}

void
XCThermLayerListWidget::StartNextQueuedDownload() noexcept
{
  while (!preload_queue.empty()) {
    const unsigned layer_index = preload_queue.front();
    preload_queue.erase(preload_queue.begin());
    StartDownload(layer_index);
    if (active_job)
      return;

    preload_queue.clear();
    return;
  }
}

void
XCThermLayerListWidget::StartDownload(unsigned layer_index) noexcept
{
  auto &settings = CommonInterface::SetComputerSettings().weather.xctherm;
  if (settings.download_span_hours == 0)
    settings.download_span_hours = 1;

  const unsigned span_hours = settings.download_span_hours;
  const auto &region = XCTherm::GetRegion(settings.model);
  if (layer_index >= region.layer_count)
    return;

  if (!settings.credentials.IsDefined()) {
    ShowMessageBox(_("Set XC Therm credentials in Config → Weather."),
                   "XC Therm", MB_OK);
    return;
  }

  auto *glue = GetXCThermDownloadGlue();
  if (glue == nullptr || Net::curl == nullptr) {
    ShowMessageBox(_("Network is not available."), "XC Therm", MB_OK);
    return;
  }

  if (glue->IsRunning()) {
    ShowMessageBox(_("Another XC Therm download is already running. "
                     "Wait for it to finish, or tap Stop."),
                   "XC Therm", MB_OK);
    return;
  }

  XCThermAPI::Instance().PrepareSession(settings);
  XCTherm::ResetAutoFetchAttempt();

  if (!XCThermAPI::Instance().EnsureAuthenticated()) {
    ShowMessageBox(_("XC Therm login failed. Check your credentials."),
                   "XC Therm", MB_OK);
    return;
  }

  active_job = XCTherm::StartSpanDownload(
    settings, layer_index,
    [this](std::shared_ptr<XCThermDownloadJob> finished) {
      active_job = std::move(finished);
      FinishDownload();
    });
  if (active_job == nullptr) {
    ShowMessageBox(_("Could not start the XC Therm download."),
                   "XC Therm", MB_OK);
    return;
  }

  auto *info = GetDownloadInfo(settings.model);
  auto &row_info = info[layer_index];
  row_info.status = LayerDownloadInfo::PENDING;
  row_info.pending_total = span_hours + 1;
  row_info.pending_index = 0;
  row_info.pending_bytes_now = 0;
  row_info.pending_bytes_total = 0;
  row_info.retry_attempt = 0;
  row_info.retry_seconds_left = 0;

  SyncButtons();
  InvalidateList();
  poll_timer.Schedule(std::chrono::milliseconds(200));
}

void
XCThermLayerListWidget::PollDownload() noexcept
{
  if (!active_job)
    return;

  auto &job = *active_job;
  auto *info = GetDownloadInfo(job.model);
  auto &row_info = info[job.target_index];
  row_info.pending_index = job.current_offset.load();
  row_info.pending_total = job.span_hours + 1;
  row_info.pending_bytes_now = job.bytes_now.load();
  row_info.pending_bytes_total = job.bytes_total.load();
  row_info.retry_attempt = job.retry_attempt.load();
  row_info.retry_seconds_left = job.retry_seconds_left.load();

  InvalidateList();
}

void
XCThermLayerListWidget::FinishDownload() noexcept
{
  if (!active_job)
    return;

  poll_timer.Cancel();

  auto job = std::move(active_job);
  active_job.reset();

  auto *info = GetDownloadInfo(job->model);
  auto &row_info = info[job->target_index];

  const unsigned span = job->span_hours;
  const unsigned ok = job->succeeded_or_cached.load();
  const unsigned nu = job->newly_downloaded.load();
  const bool canceled = job->cancel.load();
  const bool any_miss = job->any_slot_missing.load();

  if (ok == 0) {
    row_info.status = canceled
      ? LayerDownloadInfo::CANCELED
      : LayerDownloadInfo::FAILED;
    row_info.pending_index = 0;
    row_info.pending_total = 0;
    row_info.pending_bytes_now = 0;
    row_info.pending_bytes_total = 0;
    row_info.retry_attempt = 0;
    row_info.retry_seconds_left = 0;
    SyncButtons();
    InvalidateList();

    preload_queue.clear();

    if (!canceled) {
      if (job->index_no_parameters.load()) {
        ShowMessageBox(_("Forecast index has no XC Therm parameters."),
                       "XC Therm", MB_OK);
      } else if (job->error_eptr) {
        ShowError(job->error_eptr, "XC Therm");
      } else {
        ShowMessageBox(_("Forecast download failed.\nKeeping previous data."),
                       "XC Therm", MB_OK);
      }
    }
    return;
  }

  XCTherm::ApplyJobPreviewToMap(job);

  const double span_secs = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - job->started_at).count();
  const double wire_mb =
    (double)job->total_wire_bytes.load() / (1024.0 * 1024.0);
  const double speed_mbs = span_secs > 0 ? wire_mb / span_secs : 0.0;

  row_info.status = LayerDownloadInfo::DONE;
  row_info.wire_mb = wire_mb;
  row_info.speed_mbs = speed_mbs;
  row_info.span_hours = ok;
  row_info.new_downloads = nu;
  row_info.future_hours = XCThermAPI::Instance()
    .GetCachedLayerSummary(job->param).future_hours;
  row_info.pending_index = 0;
  row_info.pending_total = 0;
  row_info.pending_bytes_now = 0;
  row_info.pending_bytes_total = 0;
  row_info.retry_attempt = 0;
  row_info.retry_seconds_left = 0;

  if (job->latest_run_date.size() == 8 && job->latest_run_hour.size() == 2) {
    const std::string &d = job->latest_run_date;
    row_info.issued_utc = std::string(FmtBuffer<32>("{}-{}-{} {} UTC",
                                                    d.substr(0, 4),
                                                    d.substr(4, 2),
                                                    d.substr(6, 2),
                                                    job->latest_run_hour).c_str());
  } else {
    row_info.issued_utc = "?";
  }

  std::time_t now = std::time(nullptr);
  std::tm *lt = std::localtime(&now);
  char tbuf[16];
  if (lt != nullptr && std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", lt) > 0)
    row_info.download_time = tbuf;

  if (nu > 0) {
    const unsigned current_utc = XCTherm::GetUtcTimeParts().hour;
    const unsigned dropped =
      XCThermAPI::Instance().PruneStaleRuns(job->param, current_utc);
    if (dropped > 0)
      LogFmt("xctherm: stale-run sweep dropped {} entries for {}",
             dropped, job->param);
  }

  SyncButtons();
  InvalidateList();

  if (options_panel != nullptr)
    options_panel->RefreshStatus();

  if (nu > 0)
    PageActions::Update();

  if (job->error_eptr && !canceled) {
    ShowError(job->error_eptr, "XC Therm");
  } else if (any_miss && !canceled) {
    StaticString<128> msg;
    msg.Format(_("Got %u of %u hourly slices (%u newly downloaded).\n"
                 "Some slots were unavailable."),
               ok, span, nu);
    ShowMessageBox(msg, "XC Therm", MB_OK);
  }

  if (!canceled && !preload_queue.empty())
    StartNextQueuedDownload();
}

void
XCThermLayerListWidget::OnPaintItem(Canvas &canvas, const PixelRect rc,
                                    unsigned idx) noexcept
{
  const auto &settings =
    CommonInterface::GetComputerSettings().weather.xctherm;
  const auto &region = XCTherm::GetRegion(settings.model);
  if (idx >= region.layer_count)
    return;

  const DialogLook &look = UIGlobals::GetDialogLook();
  const bool focused = GetList().HasFocus();
  const unsigned padding = Layout::GetTextPadding();
  const unsigned box_size = rc.GetHeight() > 2 * padding
    ? rc.GetHeight() - 2 * padding
    : 0;

  PixelRect box_rc;
  box_rc.left = rc.left + (int)padding;
  box_rc.top = rc.top + (int)padding;
  box_rc.right = box_rc.left + (int)box_size;
  box_rc.bottom = box_rc.top + (int)box_size;

  DrawCheckBox(canvas, look, box_rc, IsSelected(idx), focused, false, true);

  PixelRect text_rc = rc;
  text_rc.left = box_rc.right + 2 * (int)padding;

  row_renderer.DrawFirstRow(canvas, text_rc,
                            gettext(region.layers[idx].dialog_label));
  row_renderer.DrawSecondRow(
    canvas, text_rc,
    FormatLayerStatus(settings.model, idx,
                      settings.download_span_hours).c_str());
}

void
XCThermLayerListWidget::Prepare(ContainerWindow &parent,
                                const PixelRect &rc) noexcept
{
  active = this;

  if (buttons_widget != nullptr)
    CreateButtons(buttons_widget->GetButtonPanel());

  const DialogLook &look = UIGlobals::GetDialogLook();
  CreateList(parent, look, rc,
             row_renderer.CalculateLayout(*look.list.font_bold,
                                          look.small_font));

  LoadSelection();

  MultiSelectListWidget::Prepare(parent, rc);
}

void
XCThermLayerListWidget::Show(const PixelRect &rc) noexcept
{
  MultiSelectListWidget::Show(rc);

  auto &settings = CommonInterface::SetComputerSettings().weather.xctherm;
  if (settings.download_span_hours == 0)
    settings.download_span_hours = 1;

  XCThermAPI::Instance().PrepareSession(settings);
  RehydrateRowsFromCache();

  LoadSelection();
  InvalidateList();

  if (options_panel != nullptr)
    options_panel->RefreshStatus();
}

void
XCThermLayerListWidget::Hide() noexcept
{
  SaveSelection();
  MultiSelectListWidget::Hide();
}

void
XCThermLayerListWidget::Unprepare() noexcept
{
  poll_timer.Cancel();

  if (active == this)
    active = nullptr;
  preload_button = nullptr;
  DeleteWindow();
}

} // namespace

std::unique_ptr<Widget>
CreateXCThermMainWidget() noexcept
{
  auto list = std::make_unique<XCThermLayerListWidget>();
  auto *list_ptr = list.get();
  auto buttons = std::make_unique<ButtonPanelWidget>(
    std::move(list),
    ButtonPanelWidget::Alignment::BOTTOM);
  list_ptr->SetButtonPanel(*buttons);

  auto options = std::make_unique<XCThermOptionsPanel>();
  auto *options_ptr = options.get();
  list_ptr->SetOptionsPanel(*options_ptr);
  options_ptr->SetLayerList(*list_ptr);

  return std::make_unique<TwoWidgets>(std::move(buttons),
                                      std::move(options));
}

#endif
