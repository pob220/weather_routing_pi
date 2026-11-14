// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassagePanel.h"
#include "PlanningHelp.h"
#include "PlanningTimeCtrl.h"
#include <wx/aui/auibook.h>
#include "PassagePresentation.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "weather_routing_pi.h"
#include "PassageWeather.h"
#include "BundledClimatology.h"
#include "EnvironmentalGribDialog.h"
#include "tinyxml.h"
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/menu.h>
#include <wx/textdlg.h>
#include "RouteWaypointExtractor.h"
#include <wx/srchctrl.h>
#include <wx/listbox.h>
#include <wx/statbox.h>
#include <wx/valnum.h>
#include <algorithm>
#include <map>
#include <cmath>

namespace {
wxStaticText* Text(wxWindow* p, wxSizer* s, const wxString& label,
                   bool bold = false) {
  auto* t = new wxStaticText(p, wxID_ANY, label);
  if (wxDynamicCast(p, wxDialog)) t->Wrap(560);
  if (bold) t->SetFont(t->GetFont().Bold());
  s->Add(t, 0, wxEXPAND | wxALL, 7);
  return t;
}
wxButton* Button(wxWindow* p, wxSizer* s, const wxString& label,
                 std::function<void()> fn) {
  auto* b = new wxButton(p, wxID_ANY, label);
  b->SetMinSize(wxSize(-1, 36));
  s->Add(b, 1, wxEXPAND | wxALL, 5);
  b->Bind(wxEVT_BUTTON, [fn](wxCommandEvent&) { fn(); });
  return b;
}
wxString DisplayName(wxString s) {
  s.Replace("_", " ");
  return s;
}
}  // namespace

PassagePanel::PassagePanel(wxWindow* parent, WeatherRouting& routing)
    : wxPanel(parent, wxID_ANY), routing_(routing), refresh_(this) {
  auto* outer = new wxBoxSizer(wxVERTICAL);
  content_ = new wxScrolledWindow(this, wxID_ANY);
  content_->SetMinSize(wxSize(0, 0));
  // Native controls can need more width with large fonts or long translations.
  // Keep them reachable by scrolling and keyboard focus in compact windows.
  content_->SetScrollRate(12, 14);
  auto* page = new wxBoxSizer(wxVERTICAL);
  auto* plans = new wxBoxSizer(wxHORIZONTAL);
  passages_ = new wxChoice(content_, wxID_ANY);
  passages_->SetMinSize(wxSize(160, -1));
  passages_->SetName(_("Selected passage"));
  plans->Add(passages_, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5);
  auto* add = new wxButton(content_, wxID_ANY, _("New passage"));
  plans->Add(add, 0, wxALL, 5);
  add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { NewPassage(); });
  exampleButton_ = new wxButton(content_, wxID_ANY, _("Try example passage"));
  plans->Add(exampleButton_, 0, wxALL, 5);
  exampleButton_->Bind(wxEVT_BUTTON,
                       [this](wxCommandEvent&) { NewPassage(true); });
  auto* more = new wxButton(content_, wxID_ANY, _("Passage actions"));
  plans->Add(more, 0, wxALL, 5);
  more->Bind(wxEVT_BUTTON, [this, more](wxCommandEvent&) {
    wxMenu menu;
    const int example = wxWindow::NewControlId(),
              reset = wxWindow::NewControlId();
    menu.Append(example, _("Try an example passage"));
    menu.Append(reset, _(L"Restore passage preferences…"));
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { NewPassage(true); }, example);
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { RestoreDefaults(); }, reset);
    more->PopupMenu(&menu);
  });
  mutableControls_ = {add, more, passages_, exampleButton_};
  passages_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    SelectPassage(passages_->GetSelection());
  });
  page->Add(plans, 0, wxEXPAND | wxALL, 5);
  auto card = [&](const wxString& label) {
    auto* box = new wxStaticBoxSizer(wxVERTICAL, content_, label);
    box->GetStaticBox()->SetFont(box->GetStaticBox()->GetFont().Bold());
    page->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    return box;
  };
  resultsBox_ = card(_("Passage result"));
  results_ = Text(content_, resultsBox_, "");
  failureButton_ = Button(content_, resultsBox_, _("Review failure details"),
                          [this] { ReviewFailure(); });
  failureButton_->Show(false);
  resultsBox_->Show(false);
  auto* route = card(_("1. Route"));
  route_ = Text(content_, route, "");
  route_->SetName(_("Route status"));
  auto* routeActions = new wxBoxSizer(wxHORIZONTAL);
  mutableControls_.push_back(
      Button(content_, routeActions, _("Edit route"), [this] { EditRoute(); }));
  mutableControls_.push_back(Button(content_, routeActions,
                                    _("Use OpenCPN route"),
                                    [this] { ChooseRoute(); }));
  Button(content_, routeActions, _("Waypoints and chart"),
         [this] { ShowWaypoints(); });
  route->Add(routeActions, 0, wxEXPAND);
  waypointFixButton_ = Button(content_, route, _("Edit route waypoints"),
                              [this] { EditRouteWaypoints(); });
  waypointFixButton_->Show(false);
  mutableControls_.push_back(waypointFixButton_);
  auto* boat = card(_("2. Boat and polars"));
  boat_ = Text(content_, boat, "");
  boat_->SetName(_("Boat status"));
  auto* boatActions = new wxBoxSizer(wxHORIZONTAL);
  mutableControls_.push_back(Button(content_, boatActions,
                                    _("Boat and polar library"),
                                    [this] { ShowLibrary(); }));
  mutableControls_.push_back(Button(content_, boatActions,
                                    _("Import boat / polar"),
                                    [this] { ChooseBoat(); }));
  mutableControls_.push_back(
      Button(content_, boatActions, _("Edit polars"), [this] {
        EnsurePassage();
        routing_.m_ConfigurationDialog.EditBoat();
      }));
  mutableControls_.push_back(Button(content_, boatActions, _("Boat details"),
                                    [this] { EditBoatDetails(); }));
  boat->Add(boatActions, 0, wxEXPAND);
  auto* weather = card(_("3. Weather"));
  weather_routing::SetPlanningHelp(route->GetStaticBox(), weather_routing::PlanningHelpTopic::Timing, _("Route definition and passage times."));
  weather_routing::SetPlanningHelp(boat->GetStaticBox(), weather_routing::PlanningHelpTopic::Performance, _("Boat profile, polars and vessel performance."));
  weather_routing::SetPlanningHelp(weather->GetStaticBox(), weather_routing::PlanningHelpTopic::Weather, _("Forecast and Climatology source policy."));
  auto* sourceRow = new wxBoxSizer(wxHORIZONTAL);
  auto* label = new wxStaticText(content_, wxID_ANY, _("Wind source"));
  sourceRow->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 7);
  windPolicy_ = new wxChoice(content_, wxID_ANY);
  windPolicy_->Append(_("Forecast only"));
  windPolicy_->Append(_("Climatology only"));
  windPolicy_->Append(_("Forecast with Climatology fallback"));
  windPolicy_->SetName(_("Wind source policy"));
  windPolicy_->SetToolTip(
      _("Climatology describes typical seasonal conditions. Fallback permits "
        "its use where forecast wind is unavailable."));
  sourceRow->Add(windPolicy_, 1, wxEXPAND | wxALL, 5);
  mutableControls_.push_back(windPolicy_);
  windPolicy_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    auto* r = routing_.FirstCurrentRouteMap();
    if (!r) return;
    auto c = r->GetConfiguration();
    const int policy = windPolicy_->GetSelection();
    c.UseGrib = policy != 1;
    if (policy == 0) {
      if (c.ClimatologyType > RouteMapConfiguration::CURRENTS_ONLY)
        c.ClimatologyType = c.Currents ? RouteMapConfiguration::CURRENTS_ONLY
                                       : RouteMapConfiguration::DISABLED;
    } else if (c.ClimatologyType <= RouteMapConfiguration::CURRENTS_ONLY)
      c.ClimatologyType = RouteMapConfiguration::MOST_LIKELY;
    routing_.RememberWeatherSourceChoice(c);
    routing_.ApplyPassageConfiguration(c);
    RefreshPassage();
  });
  weather->Add(sourceRow, 0, wxEXPAND);
  weather_ = Text(content_, weather, "");
  weather_->SetName(_("Weather status"));
  auto* weatherActions = new wxBoxSizer(wxHORIZONTAL);
  mutableControls_.push_back(Button(content_, weatherActions, _("Load GRIB"),
                                    [this] { LoadWeather(); }));
  mutableControls_.push_back(Button(content_, weatherActions,
                                    _("Download / generate GRIB"),
                                    [this] { GenerateWeather(); }));
  weather->Add(weatherActions, 0, wxEXPAND);
  auto* settings = new wxBoxSizer(wxHORIZONTAL);
  mutableControls_.push_back(Button(content_, settings, _("Passage settings"),
                                    [this] { OpenSettings(0); }));
  mutableControls_.push_back(Button(content_, settings, _("Routing controls"),
                                    [this] { OpenSettings(1); }));
  Button(content_, settings, _("Analysis tools..."), [this] {
    wxMenu menu;
    const auto action = [&](const wxString& label, auto function) {
      const int id = wxWindow::NewControlId();
      menu.Append(id, label);
      menu.Bind(wxEVT_MENU, [function](wxCommandEvent&) { function(); }, id);
      return id;
    };
    action(_("Routes and analysis"), [this] {
      if (routing_.m_PassageNotebook) routing_.m_PassageNotebook->SetSelection(1);
    });
    menu.AppendSeparator();
    action(_("Run diagnostics"), [this] { wxCommandEvent e; routing_.OnShowRoutingStatus(e); });
    action(_("Detailed report"), [this] { wxCommandEvent e; routing_.OnReport(e); });
    action(_("Statistics"), [this] { wxCommandEvent e; routing_.OnStatistics(e); });
    action(_("Plot"), [this] { wxCommandEvent e; routing_.OnPlot(e); });
    PopupMenu(&menu);
  });
  Button(content_, settings, _("Planning help"), [this] { ShowHelp(); });
  page->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
  limits_ = Text(content_, page, "");
  content_->SetSizer(page);
  outer->Add(content_, 1, wxEXPAND);
  auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
  status_ = Text(this, statusRow, "");
  statusRow->GetItem(status_)->SetProportion(1);
  status_->SetName(_("Calculation readiness"));
  checksButton_ = new wxButton(this, wxID_ANY, _("Review checks"));
  checksButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowChecks(); });
  statusRow->Add(checksButton_, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
  outer->Add(statusRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
  warnings_ = Text(this, outer, "");
  warnings_->SetName(_("Readiness warnings"));
  auto* recovery = new wxBoxSizer(wxHORIZONTAL);
  fixButton_ = Button(this, recovery, "", [this] {
    RefreshPassage();
    if (const auto* blocker = readiness_.FirstBlocker())
      ResolveReadinessIssue(blocker->code);
  });
  fixButton_->SetName(_("Fix calculation blocker"));
  drawSetupButton_ = Button(this, recovery, _("Draw setup"), [this] {
    weather_routing::ShowPlanningHelp(
        this, weather_routing::PlanningHelpTopic::Safety);
  });
  recovery->GetItem(drawSetupButton_)->SetProportion(0);
  fixButton_->Show(false);
  drawSetupButton_->Show(false);
  mutableControls_.push_back(fixButton_);
  mutableControls_.push_back(drawSetupButton_);
  outer->Add(recovery, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
  auto* compute = new wxBoxSizer(wxHORIZONTAL);
  calculate_ = Button(this, compute, _("Calculate route"), [this] {
    auto* r = routing_.FirstCurrentRouteMap();
    if (!r) return;
    wxCommandEvent e;
    if (!mutableControls_.front()->IsEnabled())
      routing_.OnStop(e);
    else {
      RefreshPassage();
      if (!readiness_.CanCalculate()) {
        ShowChecks();
        return;
      }
      if (r->GetConfiguration().IsMultiLegGenerated) {
        for (auto* leg : routing_.GetMultiLegGroupRoutes(
                 r->GetConfiguration().MultiLegGroupId))
          leg->Reset();
        routing_.OnComputeMultiLegSequence(e);
      } else {
        r->Reset();
        routing_.OnCompute(e);
      }
    }
    RefreshPassage();
  });
  calculate_->SetFont(calculate_->GetFont().Bold());
  calculate_->SetMinSize(wxSize(-1, 42));
  Button(this, compute, _("Detailed report"), [this] {
    wxCommandEvent e;
    routing_.OnReport(e);
  });
  outer->Add(compute, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  SetSizer(outer);
  Bind(wxEVT_HELP, [this](wxHelpEvent&) {
    weather_routing::ShowFocusedPlanningHelp(this, weather_routing::PlanningHelpTopic::Navigation);
  });
  Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
    if (IsShownOnScreen()) RefreshPassage();
  });
  refresh_.Start(1000);
  EnsurePassage();
  RefreshPassage();
}
PassagePanel::~PassagePanel() { PrepareForShutdown(); }
void PassagePanel::PrepareForShutdown() {
  refresh_.Stop();
  if (generator_) generator_->PrepareForParentShutdown();
}
void PassagePanel::EnsurePassage() {
  if (routing_.FirstCurrentRouteMap()) return;
  if (routing_.m_WeatherRoutes.empty()) {
    auto c = routing_.DefaultConfiguration();
    c.Start.clear();
    c.End.clear();
    c.StartLat = c.StartLon = c.EndLat = c.EndLon = NAN;
    routing_.AddConfiguration(c);
  }
  SelectPassage(0);
}
void PassagePanel::SelectPassage(int index) {
  if (index < 0 || index >= int(routing_.m_WeatherRoutes.size())) return;
  auto it = routing_.m_WeatherRoutes.begin();
  std::advance(it, index);
  for (long i = 0; i < routing_.m_panel->m_lWeatherRoutes->GetItemCount();
       ++i) {
    const auto* item = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(routing_.m_panel->m_lWeatherRoutes->GetItemData(i)));
    routing_.m_panel->m_lWeatherRoutes->SetItemState(
        i, item == *it ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
  }
  routing_.OnWeatherRouteSelected();
  RefreshPassage();
}
RouteMapConfiguration PassagePanel::PlanningConfiguration(
    const RouteMapConfiguration& selected) const {
  auto c = selected;
  if (c.IsMultiLegGenerated) {
    const auto legs = routing_.GetMultiLegGroupRoutes(c.MultiLegGroupId);
    if (!legs.empty()) {
      const auto first = legs.front()->GetConfiguration(),
                 last = legs.back()->GetConfiguration();
      c.Start = first.Start;
      c.StartGUID = first.StartGUID;
      c.StartLat = first.StartLat;
      c.StartLon = first.StartLon;
      c.StartTime = first.StartTime;
      c.UseCurrentTime = first.UseCurrentTime;
      c.TimeMode = first.TimeMode;
      c.PlannedArrivalTime = first.PlannedArrivalTime;
      c.ArrivalSearchHorizonMinutes = first.ArrivalSearchHorizonMinutes;
      c.End = last.End;
      c.EndGUID = last.EndGUID;
      c.EndLat = last.EndLat;
      c.EndLon = last.EndLon;
    }
  }
  for (bool start : {true, false}) {
    const auto& guid = start ? c.StartGUID : c.EndGUID;
    PlugIn_Waypoint mark;
    if (!guid.empty() && GetSingleWaypoint(guid, &mark)) {
      if (start) {
        c.StartLat = mark.m_lat;
        c.StartLon = mark.m_lon;
      } else {
        c.EndLat = mark.m_lat;
        c.EndLon = mark.m_lon;
      }
    }
  }
  return c;
}
void PassagePanel::RefreshPassage() {
  wxArrayString names;
  std::map<wxString, wxString> passageNames;
  int selected = -1, index = 0;
  auto* current = routing_.FirstCurrentRouteMap();
  for (auto* item : routing_.m_WeatherRoutes) {
    const auto c = item->routemapoverlay->GetConfiguration();
    if (c.IsMultiLegGenerated) {
      auto found = passageNames.find(c.MultiLegGroupId);
      if (found == passageNames.end()) {
        const auto passage = PlanningConfiguration(c);
        found = passageNames.emplace(c.MultiLegGroupId,
            weather_routing::PassageDisplayName(c.MultiLegParentRouteName,
                c.MultiLegParentRouteGUID, passage.Start, passage.End)).first;
      }
      names.Add(weather_routing::PassageLegLabel(found->second,
                    c.MultiLegLegIndex, c.MultiLegLegCount));
    } else {
      names.Add(c.Start.empty() ? _("New passage") : c.Start + " to " + c.End);
    }
    if (item->routemapoverlay == current) selected = index;
    ++index;
  }
  // wx 3.3 exports its string vector from the toolkit DLL. Comparing that
  // vector with newer MSVC STL helpers can import symbols absent from the
  // matched SDK. Compare via the stable wx array API instead.
  const auto displayed = passages_->GetStrings();
  bool sameNames = displayed.GetCount() == names.GetCount();
  for (size_t i = 0; sameNames && i < names.GetCount(); ++i)
    sameNames = displayed.Item(i) == names.Item(i);
  if (!sameNames) passages_->Set(names);
  if (selected >= 0) passages_->SetSelection(selected);
  if (!current) return;
  const auto c = PlanningConfiguration(current->GetConfiguration());
  const bool running = !routing_.m_RunningRouteMaps.empty() ||
                       !routing_.m_WaitingRouteMaps.empty() ||
                       routing_.m_RoutePreparationDepth ||
                       routing_.m_DeferredRoutingStartPending;
  auto input = routing_.InspectPassageInputs(c, boatModel_, climateWindReady_);
  const bool arrival = c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME;
  wxDateTime departure = c.UseCurrentTime ? wxDateTime::Now() : c.StartTime;
  if (arrival && c.PlannedArrivalTime.IsValid())
    departure = c.PlannedArrivalTime - wxTimeSpan::Minutes(c.ArrivalSearchHorizonMinutes);
  wxString when =
      arrival ? _("Target arrival: ") +
                    (c.PlannedArrivalTime.IsValid()
                         ? c.PlannedArrivalTime.Format("%d %b %Y, %H:%M",
                                                       wxDateTime::UTC)
                         : _("not set"))
              : _("Departure: ") +
                    (input.timeValid
                         ? departure.Format("%d %b %Y, %H:%M", wxDateTime::UTC)
                         : _("not set"));
  wxString route = input.positionsValid
                       ? weather_routing::PassageDirection(c.Start, c.End) + "\n" + when + " UTC"
                       : _("Choose the start, destination and departure or "
                           "target arrival time.");
  if (c.IsMultiLegGenerated) {
    route += wxString::Format(_(L"\nLeg %d of %d • "), c.MultiLegLegIndex,
                              c.MultiLegLegCount) +
             weather_routing::PassageDisplayName(c.MultiLegParentRouteName,
                 c.MultiLegParentRouteGUID, c.Start, c.End);
    route += _("\nCalculate runs this passage's legs in sequence.");
  }
  auto& provider = routing_.PassageForecast();
  const bool climate = c.ClimatologyType > RouteMapConfiguration::CURRENTS_ONLY;
  windPolicy_->SetSelection(!c.UseGrib ? (climate ? 1 : wxNOT_FOUND) : climate ? 2 : 0);
  readiness_ = weather_routing::EvaluatePassage(input);
  exampleButton_->Show(!input.positionsValid);
  const auto sectionStatus = [&](int section) {
    return weather_routing::PassageSectionStatus(readiness_.sections[section]);
  };
  bool coincident = false;
  for (const auto& issue : readiness_.sections[0].items) {
    if (issue.severity == weather_routing::DiagnosticSeverity::Error)
      route += "\n" + issue.title +
               (issue.detail.empty() ? wxString() : "\n" + issue.detail);
    coincident |= issue.code == "passage.route.coincident_waypoints";
  }
  waypointFixButton_->Show(coincident);
  route_->SetLabel(sectionStatus(0) + L" • " + route);
  wxString boat = !boatModel_.Name.empty()
                      ? boatModel_.Name
                      : DisplayName(wxFileName(c.boatFileName).GetName());
  if (!input.boatValid)
    boat = input.boatError;
  else {
    boat += wxString::Format(_("\n%d polar configuration(s)"),
                             int(boatModel_.Polars.size()));

    for (const auto& polar : boatModel_.Polars) {
      boat += "\n" + (polar.Name.empty()
                          ? DisplayName(wxFileName(polar.FileName).GetName())
                          : polar.Name);
      if (!polar.SailConfiguration.empty())
        boat += L" • " + polar.SailConfiguration;
      boat += L" • " + (polar.Source.empty()
                           ? (input.exampleBoat ? _("illustrative estimate")
                                                : _("source unknown"))
                           : polar.Source);
      if (boat.length() > 550) {
        boat += _("\nMore polars in Boat details.");
        break;
      }
    }
  }
  boat_->SetLabel(sectionStatus(1) + L" • " + boat);
  wxString weather =
      c.UseGrib ? provider.Summary() + "\n" + provider.Details()
      : climate ? _("Typical seasonal conditions, not a weather forecast")
                : _("No wind source selected");
  if (climate) weather += "\n" + weather_routing::BundledClimatology::Source();
  if (!readiness_.sections[2].items.empty() &&
      !weather.Contains(readiness_.sections[2].items.front().title))
    weather += "\n" + readiness_.sections[2].items.front().title;
  weather_->SetLabel(sectionStatus(2) + L" • " + weather);
  limits_->SetLabel(_("Passage limits: wind ") +
      weather_routing::PassageSettingValue(weather_routing::PassageSetting::TrueWind, c.MaxTrueWindKnots) +
      _(L" • waves ") + weather_routing::PassageSettingValue(weather_routing::PassageSetting::WaveHeight, c.MaxSwellMeters) +
      L" • " + (c.Currents ? _("currents on") : _("currents off")) +
      L" • " + (c.DetectLand ? _("land avoidance on") : _("land avoidance off")));
  wxString state = readiness_.Summary();
  if (current->Finished() && readiness_.CanCalculate())
    state = current->GetDiagnostics().Summary();
  status_->SetLabel(
      running ? _(L"Calculating passage… Setup is preserved if you cancel.")
              : state);
  warnings_->SetLabel(wxString::Format(
      wxPLURAL("%u warning - see checks", "%u warnings - see checks",
               readiness_.Warnings()), readiness_.Warnings()));
  warnings_->Show(!running && readiness_.Warnings() != 0);
  const auto* blocker = readiness_.FirstBlocker();
  fixButton_->Show(!running && blocker && !blocker->action.empty());
  if (blocker) fixButton_->SetLabel(blocker->action);
  const bool missingDraw = blocker &&
      blocker->code == "passage.limits.boundary_provider";
  drawSetupButton_->Show(!running && missingDraw);
  if (missingDraw)
    fixButton_->SetToolTip(_("Turn off Draw exclusions on all linked legs of this passage. Land avoidance, other passages and saved defaults stay unchanged."));
  else
    fixButton_->UnsetToolTip();
  checksButton_->SetLabel(readiness_.CanCalculate() ? _("Review checks")
      : wxString::Format(wxPLURAL("%u blocker - details", "%u blockers - details",
                                 readiness_.Blockers()), readiness_.Blockers()));
  calculate_->SetLabel(running ? _("Cancel calculation")
                               : _("Calculate route"));
  calculate_->Enable(running || readiness_.CanCalculate());
  for (auto* control : mutableControls_) control->Enable(!running);
  RefreshResults(current, running);
  if (!running && current->Finished() && readiness_.CanCalculate())
    status_->SetLabel(resultStatus_);
  for (auto* t : {route_, boat_, weather_, limits_, results_}) {
    t->Wrap(std::max(220, content_->GetClientSize().x - 64));
    // A wrapped label's previous best width must not become the scroller's
    // minimum virtual width after the user narrows the window.
    t->SetMinSize(wxSize(1, -1));
  }
  status_->Wrap(std::max(180, GetClientSize().x - 230));
  warnings_->Wrap(std::max(180, GetClientSize().x - 40));
  status_->SetMinSize(wxSize(1, -1));
  warnings_->SetMinSize(wxSize(1, -1));
  content_->Layout();
  content_->FitInside();
  Layout();
  wxString layoutTrace;
  static bool traced = false;
  if (!traced && IsShownOnScreen() && wxGetEnv("WR_UI_LAYOUT_DIAGNOSTICS", &layoutTrace)) {
    traced = true;
    const auto frame = GetParent()->GetParent()->GetClientSize();
    const auto notebook = GetParent()->GetClientSize();
    const auto size = GetClientSize();
    wxLogMessage("V2_LAYOUT frame=%dx%d notebook=%dx%d page=%dx%d content=%dx%d calculate_y=%d height=%d", frame.x, frame.y, notebook.x, notebook.y, size.x, size.y, content_->GetSize().x, content_->GetSize().y, calculate_->GetPosition().y, calculate_->GetSize().y);
  }
}

void PassagePanel::NewPassage(bool example) {
  auto* selected = routing_.FirstCurrentRouteMap();
  auto c =
      selected ? selected->GetConfiguration() : routing_.DefaultConfiguration();
  // A copied route's incidental source settings are not a remembered choice.
  // Use the last explicit weather selection, or the first-use fallback policy.
  routing_.ApplyWeatherSourceChoice(c);
  // Selecting an older route is not a new engine preference. Use the last
  // saved selector/membership choices, or Auto with every engine on first use.
  routing_.ApplyNewPassageEngineChoices(c);
  // Start a new passage, retaining the vessel and planning preferences, never
  // the identity or computed scheduling state of a linked leg or candidate.
  c.RouteGUID.clear();
  c.Start.clear();
  c.End.clear();
  c.StartGUID.clear();
  c.EndGUID.clear();
  c.StartType = RouteMapConfiguration::START_FROM_POSITION;
  c.EndType = RouteMapConfiguration::END_AT_POSITION;
  c.StartLat = c.StartLon = c.EndLat = c.EndLon = NAN;
  c.UseCurrentTime = false;
  c.StartTime = wxDateTime::Now();
  c.TimeMode = RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME;
  c.PlannedArrivalTime = c.StartTime + wxTimeSpan::Hours(24);
  c.ArrivalPlanningEvaluatedRoutes = c.ArrivalPlanningFeasibleRoutes = 0;
  c.ArrivalPlanningScheduleMarginSeconds = 0;
  c.DepartureTimeOptimizationEnabled = false;
  c.PromoteDepartureTimeOptimizationCandidate();
  c.IsMultiLegGenerated = false;
  c.MultiLegGroupId.clear();
  c.MultiLegParentRouteGUID.clear();
  c.MultiLegParentRouteName.clear();
  c.MultiLegLegIndex = c.MultiLegLegCount = 0;
  if (example) {
    c.Start = _("Example Atlantic west");
    c.End = _("Example Atlantic east");
    c.StartLat = 30;
    c.StartLon = -40;
    c.EndLat = 30.3;
    c.EndLon = -39.5;
    routing_.AddPosition(c.StartLat, c.StartLon, c.Start);
    routing_.AddPosition(c.EndLat, c.EndLon, c.End);
    c.boatFileName =
        weather_routing_pi::StandardPath() + "boats/Example-40ft-monohull.xml";
    c.UseGrib = false;
    c.ClimatologyType = RouteMapConfiguration::MOST_LIKELY;
    c.Currents = false;
    c.UseMotor = false;
    c.DetectLand = true;
    c.DetectBoundary = false;
    c.MinimumDepthMeters = 0;
    c.RequireWaveCoverage = false;
    c.MaxTrueWindKnots = weather_routing::PassageDefaults::windKnots;
    c.MaxApparentWindKnots =
        weather_routing::PassageDefaults::apparentWindKnots;
    c.MaxSwellMeters = weather_routing::PassageDefaults::waveMeters;
  }
  if (!routing_.AddConfiguration(c)) return;
  SelectPassage(int(routing_.m_WeatherRoutes.size()) - 1);
  if (!example)
    EditRoute();
  else
    ShowHelp();
}
void PassagePanel::RestoreDefaults() {
  auto* route = routing_.FirstCurrentRouteMap();
  if (!route) return;
  wxDialog dialog(this, wxID_ANY, _("Restore passage preferences"));
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       _("Restore these starting preferences for the selected passage:\n\n"
         "Maximum true wind: 30 kn\nMaximum apparent wind: 40 kn\nMaximum "
         "waves: 3 m\n"
         "Land avoidance: on\nCurrents and motoring: off\nWind and boat "
         "performance adjustments: 100%\n\n"
         "The route, passage time, boat, weather source, depth and shore "
         "margins, "
         "required wave coverage and routing controls are retained."));
  s->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
  dialog.SetSizerAndFit(s);
  dialog.CentreOnParent();
  if (dialog.ShowModal() != wxID_OK) return;
  auto c = route->GetConfiguration();
  c.MaxTrueWindKnots = weather_routing::PassageDefaults::windKnots;
  c.MaxApparentWindKnots = weather_routing::PassageDefaults::apparentWindKnots;
  c.MaxSwellMeters = weather_routing::PassageDefaults::waveMeters;
  c.DetectLand = true;
  c.Currents = false;
  c.UseMotor = false;
  c.WindStrength = c.UpwindEfficiency = c.DownwindEfficiency =
      c.NightCumulativeEfficiency = 1;
  routing_.ApplyPassageConfiguration(c);
  RefreshPassage();
}
void PassagePanel::ChooseRoute() {
  std::vector<wxString> guids;
  wxArrayString labels;
  for (const auto& guid : GetRouteGUIDArray()) {
    auto route = GetRoute_Plugin(guid);
    if (!route || !route->pWaypointList || route->pWaypointList->GetCount() < 2)
      continue;
    guids.push_back(guid);
    labels.Add((route->m_NameString.empty() ? _("Unnamed route")
                                            : route->m_NameString) +
               wxString::Format(_(L" • %zu waypoints"),
                                route->pWaypointList->GetCount()));
  }
  if (guids.empty()) {
    wxMessageBox(
        _("Create a route with at least two waypoints on the OpenCPN chart, "
          "then choose it here. You can also right-click that route to start "
          "weather routing."),
        _("Use OpenCPN route"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  wxSingleChoiceDialog chooser(
      this,
      _("Choose a route. Its waypoints become a continuous "
        "passage whose legs are calculated in sequence."),
      _("Use OpenCPN route"), labels);
  if (chooser.ShowModal() != wxID_OK) return;
  const int first = int(routing_.m_WeatherRoutes.size());
  if (routing_.CreateMultiLegConfigurationsFromRoute(
          guids.at(chooser.GetSelection()), false))
    SelectPassage(first);
}
void PassagePanel::ShowWaypoints() {
  auto* r = routing_.FirstCurrentRouteMap();
  if (!r) return;
  const auto c = r->GetConfiguration();
  const auto guid =
      c.IsMultiLegGenerated ? c.MultiLegParentRouteGUID : c.RouteGUID;
  std::vector<RouteWaypointInfo> points;
  wxString error;
  if (!guid.empty())
    ExtractOpenCPNRouteWaypoints(guid, points, error);
  else if (!c.Start.empty() && !c.End.empty()) {
    points.push_back({c.StartGUID, c.Start, c.StartLat, c.StartLon, 0});
    points.push_back({c.EndGUID, c.End, c.EndLat, c.EndLon, 1});
  }
  wxDialog dialog(this, wxID_ANY, _("Passage waypoints"), wxDefaultPosition,
                  wxSize(640, 460), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       guid.empty()
           ? _("Named positions for this passage.")
           : _("Waypoints are owned by the OpenCPN route. To edit them, show "
               "the route on "
               "the chart and open its properties. If you add, remove or "
               "reorder waypoints, "
               "use OpenCPN route again to create a new passage sequence."));
  if (!error.empty()) Text(&dialog, s, error);
  auto* list = new wxListBox(&dialog, wxID_ANY);
  list->SetName(_("Passage waypoints in order"));
  for (size_t i = 0; i < points.size(); ++i)
    list->Append(
        wxString::Format("%zu. ", i + 1) + points[i].name +
        wxString::Format("   %.5f, %.5f", points[i].lat, points[i].lon));
  s->Add(list, 1, wxEXPAND | wxALL, 10);
  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  Button(&dialog, buttons, _("Show on chart"), [&, this] {
    wxCommandEvent e;
    routing_.OnGoTo(e);
    dialog.EndModal(wxID_OK);
    routing_.Hide();
  });
  Button(&dialog, buttons, _("Close"), [&] { dialog.EndModal(wxID_CANCEL); });
  s->Add(buttons, 0, wxEXPAND | wxALL, 10);
  dialog.SetSizer(s);
  dialog.CentreOnParent();
  dialog.ShowModal();
}
void PassagePanel::EditRouteWaypoints() {
  auto* selected = routing_.FirstCurrentRouteMap();
  if (!selected) return;
  const auto c = selected->GetConfiguration();
  const wxString guid = c.IsMultiLegGenerated ? c.MultiLegParentRouteGUID : c.RouteGUID;
  std::vector<RouteWaypointInfo> points;
  wxString error;
  if (!ExtractOpenCPNRouteWaypoints(guid, points, error)) {
    wxMessageBox(error, _("Edit route waypoints"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  wxDialog dialog(this, wxID_ANY, _("Edit route waypoints"), wxDefaultPosition,
                  wxSize(660, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* body = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, body, _("Select a waypoint to move. Saving changes its position in OpenCPN and in every route that shares it. To remove a waypoint, show it on the chart and use OpenCPN's route-editing controls."));
  auto* list = new wxListBox(&dialog, wxID_ANY);
  list->SetName(_("Route waypoints to edit"));
  for (const auto& point : points)
    list->Append(wxString::Format("%d. %s", point.index, point.name));
  body->Add(list, 1, wxEXPAND | wxALL, 8);
  auto* fields = new wxFlexGridSizer(2, 8, 8);
  fields->AddGrowableCol(1);
  fields->Add(new wxStaticText(&dialog, wxID_ANY, _("Latitude (decimal degrees)")));
  auto* lat = new wxTextCtrl(&dialog, wxID_ANY);
  lat->SetName(_("Waypoint latitude"));
  fields->Add(lat, 1, wxEXPAND);
  fields->Add(new wxStaticText(&dialog, wxID_ANY, _("Longitude (decimal degrees)")));
  auto* lon = new wxTextCtrl(&dialog, wxID_ANY);
  lon->SetName(_("Waypoint longitude"));
  fields->Add(lon, 1, wxEXPAND);
  body->Add(fields, 0, wxEXPAND | wxALL, 8);
  auto showPosition = [&] {
    const auto& point = points.at(list->GetSelection());
    lat->SetValue(weather_routing::WaypointCoordinateText(point.lat));
    lon->SetValue(weather_routing::WaypointCoordinateText(point.lon));
  };
  int initial = 0;
  for (size_t i = 1; i < points.size(); ++i)
    if (weather_routing::PositionsCoincide(points[i-1].lat, points[i-1].lon,
                                         points[i].lat, points[i].lon)) {
      initial = int(i);
      break;
    }
  list->SetSelection(initial);
  showPosition();
  list->Bind(wxEVT_LISTBOX, [&](wxCommandEvent&) { showPosition(); });
  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  Button(&dialog, buttons, _("Show on chart"), [&] {
    const auto& point = points.at(list->GetSelection());
    JumpToPosition(point.lat, point.lon, .02);
    dialog.EndModal(wxID_CANCEL);
    routing_.Hide();
  });
  Button(&dialog, buttons, _("Save waypoint position"), [&] {
    double a, b;
    if (!lat->GetValue().ToDouble(&a) || !lon->GetValue().ToDouble(&b) ||
        !std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 90 || std::abs(b) > 180) {
      wxMessageBox(_("Enter latitude from -90 to 90 and longitude from -180 to 180."),
                   _("Invalid waypoint position"), wxOK | wxICON_INFORMATION, &dialog);
      return;
    }
    const auto& point = points.at(list->GetSelection());
    if (a == point.lat && b == point.lon) { dialog.EndModal(wxID_CANCEL); return; }
    PlugIn_Waypoint latest;
    if (!GetSingleWaypoint(point.guid, &latest) ||
        latest.m_lat != point.lat || latest.m_lon != point.lon) {
      wxMessageBox(_("This waypoint changed while the editor was open. Close and reopen the editor before saving."),
                   _("Waypoint changed"), wxOK | wxICON_INFORMATION, &dialog);
      return;
    }
    // Read fresh metadata and change only the coordinates. Do not replace the
    // route: the V1 route-update API deletes/recreates it and loses properties.
    latest.m_lat = a;
    latest.m_lon = b;
    if (!UpdateSingleWaypoint(&latest)) {
      wxMessageBox(_("OpenCPN could not update this waypoint."), _("Waypoint not saved"),
                   wxOK | wxICON_INFORMATION, &dialog);
      return;
    }
    RequestRefresh(GetOCPNCanvasWindow());
    dialog.EndModal(wxID_OK);
  });
  body->Add(buttons, 0, wxEXPAND | wxALL, 8);
  body->Add(dialog.CreateButtonSizer(wxCANCEL), 0, wxEXPAND | wxALL, 8);
  dialog.SetSizer(body);
  dialog.CentreOnParent();
  if (dialog.ShowModal() == wxID_OK) {
    // Existing settings and GUIDs are kept. Position resolution on the next
    // readiness check/calculation reads the newly saved OpenCPN coordinates.
    for (auto* route : routing_.GetMultiLegGroupRoutes(c.MultiLegGroupId)) {
      auto leg = route->GetConfiguration();
      leg.Update();
      route->Reset();
      route->SetConfiguration(leg);
      routing_.UpdateRouteMap(route);
    }
  }
  RefreshPassage();
}
void PassagePanel::OpenSettings(size_t section) {
  EnsurePassage();
  routing_.m_ConfigurationDialog.ShowSettingsSection(section);
  auto* route = routing_.FirstCurrentRouteMap();
  if (route && route->GetConfiguration().IsMultiLegGenerated) {
    wxCommandEvent event;
    routing_.OnEditMultiLegGroupSettings(event);
  } else
    routing_.OnEditConfiguration();
}
void PassagePanel::ShowChecks() {
  RefreshPassage();
  wxDialog dialog(this, wxID_ANY, _("Passage checks"), wxDefaultPosition,
                  wxSize(680, 540), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  auto* scroll = new wxScrolledWindow(&dialog);
  scroll->SetScrollRate(0, 14);
  auto* body = new wxBoxSizer(wxVERTICAL);
  Text(scroll, body, readiness_.Summary(), true)->Wrap(600);
  // Show every blocker before warnings, regardless of its setup section.
  for (const auto severity : {weather_routing::DiagnosticSeverity::Error,
                              weather_routing::DiagnosticSeverity::Warning,
                              weather_routing::DiagnosticSeverity::Notice}) {
    bool heading = false;
    for (const auto& issue : readiness_.Report().items) {
      if (issue.severity != severity) continue;
      if (!heading) {
        Text(scroll, body,
             severity == weather_routing::DiagnosticSeverity::Error
                 ? _("Fix before calculating")
                 : severity == weather_routing::DiagnosticSeverity::Warning
                       ? _("Warnings - do not prevent calculation")
                       : _("Information"), true);
        heading = true;
      }
      auto* text =
          Text(scroll, body,
               issue.title + (issue.detail.empty() ? wxString() : "\n" + issue.detail));
      text->Wrap(600);
      if (issue.action.empty()) continue;
      auto* action = new wxButton(scroll, wxID_ANY, issue.action);
      body->Add(action, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
      action->Enable(mutableControls_.front()->IsEnabled());
      action->Bind(wxEVT_BUTTON, [&, code = issue.code](wxCommandEvent&) {
        dialog.EndModal(wxID_OK);
        // Defer until the modal stack unwinds, and resolve current selection at
        // execution rather than retaining pointers to route models.
        CallAfter([this, code] { ResolveReadinessIssue(code); });
      });
      if (issue.code == "passage.limits.boundary_provider") {
        auto* setup = new wxButton(scroll, wxID_ANY, _("Draw setup"));
        body->Add(setup, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        setup->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
          weather_routing::ShowPlanningHelp(
              &dialog, weather_routing::PlanningHelpTopic::Safety);
        });
      }
    }
  }
  if (auto* route = routing_.FirstCurrentRouteMap();
      route && route->Finished()) {
    const auto report = route->GetDiagnostics();
    Text(scroll, body, _("Last calculation: ") + report.Summary(), true)
        ->Wrap(600);
    for (const auto& item : report.items)
      if (item.severity != weather_routing::DiagnosticSeverity::Notice)
        Text(scroll, body,
             item.title + (item.detail.empty() ? wxString() : "\n" + item.detail))
            ->Wrap(600);
    auto* details = new wxButton(scroll, wxID_ANY, _("Open detailed report"));
    body->Add(details, 0, wxALL, 8);
    details->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
      dialog.EndModal(wxID_OK);
      CallAfter([this] {
        wxCommandEvent e;
        routing_.OnReport(e);
      });
    });
  }
  Text(scroll, body,
       _("These checks establish whether calculation can start. "
         "Weather coverage, detours and constraints are also checked during "
         "routing and final validation."))
      ->Wrap(600);
  scroll->SetSizer(body);
  s->Add(scroll, 1, wxEXPAND);
  s->Add(dialog.CreateButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 10);
  dialog.SetSizer(s);
  dialog.CentreOnParent();
  dialog.ShowModal();
}
void PassagePanel::ResolveReadinessIssue(const std::string& code) {
  if (code == "passage.limits.boundary_provider") {
    routing_.DisablePassageDrawExclusions();
    RefreshPassage();
  } else if (code == "passage.weather.waves" ||
             code == "passage.weather.destination")
    GenerateWeather();
  else if (code == "passage.route.coincident_waypoints")
    EditRouteWaypoints();
  else if (code == "passage.route.sequence")
    ChooseRoute();
  else
    switch (weather_routing::RecoveryAction(code)) {
      case weather_routing::PassageAction::Route: EditRoute(); break;
      case weather_routing::PassageAction::Boat: ShowLibrary(); break;
      case weather_routing::PassageAction::Weather: windPolicy_->SetFocus(); break;
      case weather_routing::PassageAction::Limits: OpenSettings(0); break;
    }
}
void PassagePanel::ShowHelp() {
  weather_routing::ShowPlanningHelp(this, weather_routing::PlanningHelpTopic::Navigation);
}

void PassagePanel::RefreshResults(RouteMapOverlay* current, bool busy) {
  if (busy || !current->Finished()) {
    resultsBox_->Show(false);
    resultRoute_ = nullptr;
    resultKey_.clear();
    resultStatus_.clear();
    return;
  }
  const auto config = current->GetConfiguration();
  auto routes = config.IsMultiLegGenerated
                    ? routing_.GetMultiLegGroupRoutes(config.MultiLegGroupId)
                    : std::vector<RouteMapOverlay*>{current};
  wxString key;
  for (auto* r : routes)
    key +=
        r->GetDiagnostics().Summary() + r->ModernNativeCoverageNotes() +
        (r->EndTime().IsValid() ? r->EndTime().FormatISOCombined() : wxString("pending"));
  if (current == resultRoute_ && key == resultKey_) return;
  resultRoute_ = current;
  resultKey_ = key;
  unsigned complete = 0, warnings = 0;
  double distance = 0, wind = NAN, waves = NAN;
  bool missingWaves = false, usedClimate = false, missingWind = false;
  wxDateTime start, end;
  for (auto* r : routes) {
    const auto report = r->GetDiagnostics();
    warnings += report.Count(weather_routing::DiagnosticSeverity::Warning);
    if (!r->Finished() || !r->ReachedDestination()) continue;
    ++complete;
    missingWaves = missingWaves || r->HasUnknownWaveHeights() ||
                   !r->GetConfiguration().UseGrib;
    distance += r->RouteInfo(RouteMapOverlay::DISTANCE);
    const double w = r->RouteInfo(RouteMapOverlay::MAXWIND),
                 h = r->RouteInfo(RouteMapOverlay::MAXSWELL);
    if (std::isfinite(w)) wind = std::isfinite(wind) ? std::max(wind, w) : w;
    if (std::isfinite(h) && h >= 0)
      waves = std::isfinite(waves) ? std::max(waves, h) : h;
    if (!start.IsValid() || r->StartTime() < start) start = r->StartTime();
    if (!end.IsValid() || r->EndTime() > end) end = r->EndTime();
    for (const auto& item : report.items) {
      usedClimate = usedClimate || item.code == "usage.climatology_wind";
      missingWind = missingWind || item.code == "usage.missing_wind";
      missingWaves = missingWaves || item.code == "usage.missing_waves";
    }
  }
  wxString summary;
  if (!complete) summary = _("No completed passage.");
  else {
    if (routes.size() > 1)
      summary += wxString::Format(_("%u of %zu legs complete\n"), complete,
                                  routes.size());
    summary += (complete == routes.size() ? _("Arrival: ") : _("Last completed leg ends: ")) + end.Format("%d %b %Y, %H:%M", wxDateTime::UTC) +
               " UTC";
    const auto duration = end - start;
    summary += wxString::Format(
        _(L" • Duration: %ld h %ld min\nDistance: %.1f nm"),
        static_cast<long>(duration.GetHours()),
        static_cast<long>(duration.GetMinutes() % 60), distance);
    const double hours = duration.GetSeconds().ToDouble() / 3600;
    if (hours > 0)
      summary += wxString::Format(_(L" • Average over ground: %.1f kn"),
                                  distance / hours);
    summary +=
        _("\nMaximum wind: ") + (std::isfinite(wind) && !missingWind
                                     ? wxString::Format(_("%.1f kn"), wind)
                                     : _("not fully available"));
    summary +=
        _(L" • Maximum waves: ") + (std::isfinite(waves) && !missingWaves
                                       ? wxString::Format(_("%.1f m"), waves)
                                       : _("not fully available"));
    summary +=
        _("\nWind used: ") +
        (usedClimate ? _("Climatology was used; intervals are in the report")
         : current->UsesModernNativeResult() && config.UseGrib && !missingWind
             ? _("forecast")
         : !config.UseGrib ? _("Climatology")
                           : _("see detailed report"));
    summary += wxString::Format(_(L" • %u warnings"), warnings);
  }
  bool showFailureReview = false;
  if (complete < routes.size()) {
    // Report the first failed leg before later legs waiting for its result.
    RouteMapOverlay* failed = nullptr;
    for (auto* r : routes)
      if (r->Finished() && !r->ReachedDestination()) { failed = r; break; }
    if (failed) {
      const auto report = failed->GetDiagnostics();
      if (routes.size() > 1)
        summary += wxString::Format(_("\nLeg %d: "), failed->GetConfiguration().MultiLegLegIndex);
      else summary += "\n";
      if (const auto reason = report.PrimaryFailure()) {
        summary += reason->title;
        // The first explanatory line belongs here; the full per-engine
        // transcript remains available in the detailed report.
        const auto detail = reason->detail.BeforeFirst('\n');
        if (!detail.empty() && detail != reason->title) summary += "\n" + detail;
        if (!reason->action.empty()) summary += "\n" + reason->action;
        using Review = weather_routing::DiagnosticReview;
        wxString label = _("Review failure details");
        switch (report.FailureReview()) {
          case Review::Weather: label = _("Review weather"); break;
          case Review::Boat: label = _("Review boat and polars"); break;
          case Review::Route: label = _("Review route"); break;
          case Review::RoutingControls: label = _("Review routing controls"); break;
          default: break;
        }
        failureButton_->SetLabel(label);
        showFailureReview = true;
      } else if (report.cancelled)
        summary += _("Stopped by user. Setup is preserved; calculate again when ready.");
      else {
        summary += report.Summary() + _("\nThe engine did not identify a specific cause. Review the detailed report.");
        failureButton_->SetLabel(_("Review failure details"));
        showFailureReview = true;
      }
    } else summary += _("\nRemaining legs have not completed.");
  }
  resultStatus_ =
      routes.size() > 1
          ? complete == routes.size()
                ? wxString::Format(
                      _(L"Passage complete • %u warnings across %zu legs"),
                      warnings, routes.size())
                : wxString::Format(
                      _(L"Passage incomplete • %u of %zu legs complete"),
                      complete, routes.size())
          : current->GetDiagnostics().Summary();
  results_->SetLabel(summary);
  resultsBox_->Show(true);
  // Showing a sizer reveals all its children; apply conditional visibility last.
  failureButton_->Show(showFailureReview);
}


void PassagePanel::ReviewFailure() {
  auto* current = routing_.FirstCurrentRouteMap();
  if (!current || current->Running()) return;
  const auto config = current->GetConfiguration();
  auto routes = config.IsMultiLegGenerated
      ? routing_.GetMultiLegGroupRoutes(config.MultiLegGroupId)
      : std::vector<RouteMapOverlay*>{current};
  for (auto* route : routes) {
    if (!route->Finished() || route->ReachedDestination()) continue;
    const auto target = route->GetDiagnostics().FailureReview();
    int index = 0;
    for (auto* item : routing_.m_WeatherRoutes) {
      if (item->routemapoverlay == route) { SelectPassage(index); break; }
      ++index;
    }
    using Review = weather_routing::DiagnosticReview;
    switch (target) {
      case Review::Weather: windPolicy_->SetFocus(); ShowChecks(); break;
      case Review::Boat: ShowLibrary(); break;
      case Review::Route:
        if (const auto failure = route->GetDiagnostics().PrimaryFailure();
            failure && failure->code == "setup.coincident_waypoints")
          EditRouteWaypoints();
        else EditRoute();
        break;
      case Review::RoutingControls: OpenSettings(1); break;
      default: { wxCommandEvent event; routing_.OnReport(event); break; }
    }
    return;
  }
}

void PassagePanel::EditRoute() {
  EnsurePassage();
  auto* r = routing_.FirstCurrentRouteMap();
  if (!r) return;
  auto c = r->GetConfiguration();
  if (c.IsMultiLegGenerated && c.MultiLegLegIndex > 1) {
    int index = 0;
    for (auto* item : routing_.m_WeatherRoutes) {
      const auto first = item->routemapoverlay->GetConfiguration();
      if (first.MultiLegGroupId == c.MultiLegGroupId &&
          first.MultiLegLegIndex == 1) {
        SelectPassage(index);
        EditRoute();
        return;
      }
      ++index;
    }
  }
  std::vector<RouteMapPosition> choices(RouteMap::Positions.begin(),
                                        RouteMap::Positions.end());
  for (const auto& guid : GetWaypointGUIDArray()) {
    PlugIn_Waypoint mark;
    if (!GetSingleWaypoint(guid, &mark)) continue;
    bool duplicate = false;
    for (const auto& p : choices) duplicate = duplicate || p.GUID == guid;
    if (!duplicate)
      choices.emplace_back(mark.m_MarkName, mark.m_lat, mark.m_lon, guid);
  }
  wxDialog dialog(this, wxID_ANY, _("Define passage"), wxDefaultPosition,
                  wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  const bool linked = !c.RouteGUID.empty() || c.IsMultiLegGenerated;
  auto* top = new wxBoxSizer(wxVERTICAL);
  Text(
      &dialog, top,
      _("Select saved positions or enter coordinates in decimal degrees. Chart "
        "positions can also be added from the chart's right-click menu."));
  auto* grid = new wxFlexGridSizer(2, 8, 8);
  grid->AddGrowableCol(1);
  wxComboBox* name[2];
  wxTextCtrl* lat[2];
  wxTextCtrl* lon[2];
  for (int i = 0; i < 2; ++i) {
    grid->Add(
        new wxStaticText(&dialog, wxID_ANY, i ? _("Destination") : _("Start")),
        0, wxALIGN_CENTER_VERTICAL);
    name[i] = new wxComboBox(&dialog, wxID_ANY, i ? c.End : c.Start);
    for (const auto& p : choices)
      name[i]->Append(p.Name + (p.GUID.empty() ? wxString() : _(" (waypoint)")));
    grid->Add(name[i], 1, wxEXPAND);
    grid->Add(
        new wxStaticText(&dialog, wxID_ANY, _(L"Latitude (north +, south −)")),
        0, wxALIGN_CENTER_VERTICAL);
    lat[i] =
        new wxTextCtrl(&dialog, wxID_ANY,
                       std::isfinite(i ? c.EndLat : c.StartLat)
                           ? wxString::Format("%.6f", i ? c.EndLat : c.StartLat)
                           : wxString());
    grid->Add(lat[i], 1, wxEXPAND);
    grid->Add(
        new wxStaticText(&dialog, wxID_ANY, _(L"Longitude (east +, west −)")), 0,
        wxALIGN_CENTER_VERTICAL);
    lon[i] =
        new wxTextCtrl(&dialog, wxID_ANY,
                       std::isfinite(i ? c.EndLon : c.StartLon)
                           ? wxString::Format("%.6f", i ? c.EndLon : c.StartLon)
                           : wxString());
    grid->Add(lon[i], 1, wxEXPAND);
    if (linked) {
      name[i]->Enable(false);
      lat[i]->Enable(false);
      lon[i]->Enable(false);
    }
    name[i]->Bind(wxEVT_COMBOBOX, [&, i](wxCommandEvent&) {
      const int selected = name[i]->GetSelection();
      if (selected >= 0) {
        const auto& p = choices.at(selected);
        lat[i]->SetValue(wxString::Format("%.6f", p.lat));
        lon[i]->SetValue(wxString::Format("%.6f", p.lon));
      }
    });
  }
  auto* mode = new wxChoice(&dialog, wxID_ANY);
  mode->Append(_("Depart at"));
  mode->Append(_("Arrive by"));
  mode->SetSelection(
      c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME ? 1 : 0);
  grid->Add(mode, 0, wxEXPAND);
  wxDateTime pendingTimes[] = {c.UseCurrentTime ? wxDateTime::Now() : c.StartTime,
                               c.PlannedArrivalTime};
  int shownMode = mode->GetSelection();
  auto* time = new PlanningTimeCtrl(&dialog, pendingTimes[shownMode],
      _("Passage time"), routing_.m_SettingsDialog.UseLocalTimeZone(),
      routing_.m_SettingsDialog.DisplayTimeZone());
  mode->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) {
    wxString error;
    const auto instant = time->GetUtc(&error);
    if (!instant.IsValid()) {
      mode->SetSelection(shownMode);
      wxMessageBox(error, _("Check passage time"), wxOK | wxICON_INFORMATION, &dialog);
      return;
    }
    pendingTimes[shownMode] = instant;
    shownMode = mode->GetSelection();
    time->SetUtc(pendingTimes[shownMode].IsValid() ? pendingTimes[shownMode]
                                                 : wxDateTime::Now());
  });
  grid->Add(time, 1, wxEXPAND);
  top->Add(grid, 1, wxEXPAND | wxALL, 15);
  Text(&dialog, top,
       _("For intermediate waypoints, right-click an OpenCPN route and choose "
         "Create Weather Routing Legs."));
  if (linked)
    Text(&dialog, top,
         _("These waypoints follow the OpenCPN route. Edit that route on the "
           "chart to change its waypoints. A continuous passage starts at its "
           "first leg."));
  top->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
  dialog.SetSizerAndFit(top);
  dialog.SetMinSize(wxSize(620, -1));
  dialog.CentreOnParent();
  auto* ok = dialog.FindWindow(wxID_OK);
  ok->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    double a[2], b[2];
    wxString timeError;
    const auto date = time->GetUtc(&timeError);
    if (!date.IsValid()) {
      wxMessageBox(timeError, _("Check passage time"),
                   wxOK | wxICON_INFORMATION, &dialog);
      return;
    }
    pendingTimes[mode->GetSelection()] = date;
    const auto applyTiming = [&] {
      c.UseCurrentTime = false;
      c.TimeMode = mode->GetSelection()
                       ? RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                       : RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME;
      if (pendingTimes[0].IsValid()) c.StartTime = pendingTimes[0];
      if (pendingTimes[1].IsValid()) c.PlannedArrivalTime = pendingTimes[1];
    };
    if (linked) {
      applyTiming();
      dialog.EndModal(wxID_OK);
      return;
    }
    bool valid = true;
    for (int i = 0; i < 2; ++i)
      valid = lat[i]->GetValue().ToDouble(&a[i]) &&
              lon[i]->GetValue().ToDouble(&b[i]) && std::isfinite(a[i]) &&
              std::isfinite(b[i]) && std::abs(a[i]) <= 90 &&
              std::abs(b[i]) <= 180 && !name[i]->GetValue().empty() && valid;
    if (!valid) {
      wxMessageBox(_("Enter two named positions with valid coordinates."),
                   _("Check passage"), wxOK | wxICON_INFORMATION, &dialog);
      return;
    }
    if (a[0] == a[1] && b[0] == b[1]) {
      wxMessageBox(_("Start and destination must be different."),
                   _("Check passage"), wxOK, &dialog);
      return;
    }
    if (name[0]->GetValue() == name[1]->GetValue()) {
      wxMessageBox(_("Give the start and destination different names."),
                   _("Check passage"), wxOK, &dialog);
      return;
    }
    wxString guids[2];
    for (int i = 0; i < 2; ++i) {
      const int selected = name[i]->GetSelection();
      if (selected >= 0) {
        const auto& point = choices.at(selected);
        if (std::abs(point.lat - a[i]) < 1e-6 &&
            std::abs(point.lon - b[i]) < 1e-6 && !point.GUID.empty()) {
          guids[i] = point.GUID;
          name[i]->SetValue(point.Name);
          continue;
        }
      }
      bool exists = false;
      for (const auto& p : RouteMap::Positions)
        if (p.Name == name[i]->GetValue()) {
          if (p.lat == a[i] && p.lon == b[i])
            exists = true;
          else
            name[i]->SetValue(
                name[i]->GetValue() +
                wxString::Format(" (%lld)", wxGetUTCTimeMillis().GetValue()));
          break;
        }
      if (!exists) routing_.AddPosition(a[i], b[i], name[i]->GetValue());
    }
    c.Start = name[0]->GetValue();
    c.End = name[1]->GetValue();
    c.StartLat = a[0];
    c.StartLon = b[0];
    c.EndLat = a[1];
    c.EndLon = b[1];
    c.StartType = guids[0].empty() ? RouteMapConfiguration::START_FROM_POSITION
                                   : RouteMapConfiguration::START_FROM_WAYPOINT;
    c.EndType = guids[1].empty() ? RouteMapConfiguration::END_AT_POSITION
                                 : RouteMapConfiguration::END_AT_WAYPOINT;
    c.StartGUID = guids[0];
    c.EndGUID = guids[1];
    applyTiming();
    dialog.EndModal(wxID_OK);
  });
  if (dialog.ShowModal() == wxID_OK) routing_.ApplyPassageConfiguration(c);
  RefreshPassage();
}

void PassagePanel::ApplyBoat(const wxString& path) {
  EnsurePassage();
  auto* r = routing_.FirstCurrentRouteMap();
  if (!r) return;
  auto c = r->GetConfiguration();
  c.boatFileName = path;
  boatModel_.OpenXML(path, false, false);
  routing_.ApplyPassageConfiguration(c);
  RefreshPassage();
}
void PassagePanel::ChooseBoat() {
  wxFileDialog chooser(
      this, _("Import boat or polar"), wxEmptyString, wxEmptyString,
      _("Boat and polar files|*.xml;*.pol;*.csv;*.txt|All files|*.*"),
      wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (chooser.ShowModal() != wxID_OK) return;
  if (wxFileName(chooser.GetPath()).GetExt().CmpNoCase("xml") == 0) {
    wxString imported;
    const wxString error = Boat::ImportXML(
        chooser.GetPath(), weather_routing_pi::StandardPath(), imported);
    if (!error.empty()) {
      wxMessageBox(error, _("Cannot import boat"), wxOK | wxICON_INFORMATION,
                   this);
      return;
    }
    ApplyBoat(imported);
    return;
  }
  Polar p;
  wxString error;
  if (!p.Open(chooser.GetPath(), error)) {
    wxMessageBox(error, _("Cannot import polar"), wxOK | wxICON_ERROR, this);
    return;
  }
  const wxString folder = weather_routing_pi::StandardPath();
  wxString target =
      folder + "polars/" + wxFileName(chooser.GetPath()).GetFullName();
  if (wxFileExists(target))
    target = folder + "polars/" +
             wxString::Format("import-%lld-", wxGetUTCTimeMillis().GetValue()) +
             wxFileName(chooser.GetPath()).GetFullName();
  if (!wxCopyFile(chooser.GetPath(), target, false)) {
    wxMessageBox(_("Unable to copy this polar into your boat library."));
    return;
  }
  Boat boat;
  p.FileName = target;
  boat.Polars.push_back(p);
  const wxString path =
      folder + "boats/" +
      wxString::Format("import-%lld.xml", wxGetUTCTimeMillis().GetValue());
  error = boat.SaveXML(path);
  if (!error.empty()) {
    wxMessageBox(error);
    return;
  }
  ApplyBoat(path);
}
void PassagePanel::ShowLibrary() {
  const wxString root = weather_routing_pi::StandardPath();
  wxDialog dialog(this, wxID_ANY, _("Boat and polar library"),
                  wxDefaultPosition, wxSize(700, 650),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       _("Select a saved boat or use a library polar as a starting point. "
         "Library polars are copied before editing. Source information may be "
         "unknown."));
  auto* kind = new wxChoice(&dialog, wxID_ANY);
  kind->Append(_("Saved boats"));
  kind->Append(_("Polar library"));
  kind->SetSelection(0);
  kind->SetName(_("Library category"));
  s->Add(kind, 0, wxEXPAND | wxALL, 8);
#ifdef __OCPN__ANDROID__
  // The Android wxQt host does not export wxSearchCtrl.
  auto* search = new wxTextCtrl(&dialog, wxID_ANY);
#else
  auto* search = new wxSearchCtrl(&dialog, wxID_ANY);
#endif
  search->SetName(_("Search boat and polar names"));
  s->Add(search, 0, wxEXPAND | wxALL, 8);
  auto* list = new wxListBox(&dialog, wxID_ANY);
  list->SetName(_("Available boats and polars"));
  s->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
  auto* details = Text(&dialog, s, "");
  auto* actions = new wxBoxSizer(wxHORIZONTAL);
  auto* duplicate = new wxButton(&dialog, wxID_ANY, _("Duplicate boat"));
  auto* exportBoat = new wxButton(&dialog, wxID_ANY, _(L"Export boat…"));
  actions->Add(duplicate, 1, wxALL, 5);
  actions->Add(exportBoat, 1, wxALL, 5);
  s->Add(actions, 0, wxEXPAND | wxALL, 5);
  s->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
  if (auto* ok = dialog.FindWindow(wxID_OK)) ok->SetLabel(_("Use selection"));
  std::vector<wxString> filtered;
  const auto describe = [&] {
    const int index = list->GetSelection();
    const bool selected = index >= 0;
    duplicate->Enable(selected && kind->GetSelection() == 0);
    exportBoat->Enable(selected && kind->GetSelection() == 0);
    dialog.FindWindow(wxID_OK)->Enable(selected);
    wxString value;
    if (selected) {
      const auto path = filtered.at(index);
      if (kind->GetSelection() == 0) {
        TiXmlDocument doc;
        if (doc.LoadFile(path.utf8_str()) && doc.RootElement()) {
          value =
              wxString::FromUTF8(doc.RootElement()->Attribute("description"));
          for (auto* p = doc.RootElement()->FirstChildElement("Polar"); p;
               p = p->NextSiblingElement("Polar")) {
            const auto name = wxString::FromUTF8(p->Attribute("Name"));
            const auto source = wxString::FromUTF8(p->Attribute("Source"));
            const auto sails = wxString::FromUTF8(p->Attribute("Sails"));
            if (!value.empty()) value += "\n";
            value +=
                name.empty()
                    ? wxFileName(wxString::FromUTF8(p->Attribute("FileName")))
                          .GetName()
                    : name;
            value += L" • " + (source.empty() ? _("source unknown") : source);
            if (!sails.empty()) value += L" • " + sails;
            if (value.length() > 350) {
              value += _("\nMore details after selection.");
              break;
            }
          }
        }
      } else
        value =
            _("A personal copy will be created. Source: unknown unless "
              "documented. "
              "Add provenance and sail information in Boat details.");
    }
    details->SetLabel(value);
    details->Wrap(640);
    dialog.Layout();
  };
  const auto filter = [&] {
    wxArrayString paths;
    wxDir::GetAllFiles(root + (kind->GetSelection() == 0 ? "boats" : "polars"),
                       &paths, kind->GetSelection() == 0 ? "*.xml" : "*.pol",
                       wxDIR_FILES | wxDIR_DIRS);
    paths.Sort();
    filtered.clear();
    list->Clear();
    for (const auto& path : paths) {
      wxString name = DisplayName(wxFileName(path).GetName());
      if (kind->GetSelection() == 0) {
        TiXmlDocument doc;
        if (doc.LoadFile(path.utf8_str()) && doc.RootElement()) {
          const auto title =
              wxString::FromUTF8(doc.RootElement()->Attribute("name"));
          if (!title.empty()) name = title;
        }
      }
      if (name.Lower().Contains(search->GetValue().Lower())) {
        filtered.push_back(path);
        list->Append(name);
      }
    }
    if (list->GetCount()) list->SetSelection(0);
    describe();
  };
  kind->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { filter(); });
  search->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { filter(); });
  list->Bind(wxEVT_LISTBOX, [&](wxCommandEvent&) { describe(); });
  duplicate->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    const int index = list->GetSelection();
    if (index < 0) return;
    wxString imported;
    wxString error = Boat::ImportXML(filtered.at(index), root, imported);
    if (error.empty()) {
      Boat copy;
      error = copy.OpenXML(imported, false, false);
      if (error.empty()) {
        copy.Name = list->GetStringSelection() + _(" copy");
        error = copy.SaveXML(imported);
      }
    }
    if (!error.empty()) {
      wxMessageBox(error, _("Cannot duplicate boat"), wxOK, &dialog);
      return;
    }
    filter();
    for (size_t i = 0; i < filtered.size(); ++i)
      if (filtered[i] == imported) list->SetSelection(i);
    describe();
  });
  exportBoat->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    const int index = list->GetSelection();
    if (index < 0) return;
    wxFileDialog save(&dialog, _("Export boat and polars"), "", "boat.xml",
                      _("Boat XML|*.xml"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (save.ShowModal() != wxID_OK) return;
    const wxString error = Boat::ExportXML(filtered.at(index), save.GetPath());
    wxMessageBox(error.empty()
                     ? _("Boat exported with its polars in a folder beside the "
                         "XML file. Keep them together when sharing.")
                     : error,
                 error.empty() ? _("Boat exported") : _("Cannot export boat"),
                 wxOK, &dialog);
  });
  dialog.SetSizer(s);
  filter();
  dialog.CentreOnParent();
  search->SetFocus();
  if (dialog.ShowModal() != wxID_OK || list->GetSelection() < 0) return;
  const wxString selected = filtered.at(list->GetSelection());
  if (kind->GetSelection() == 0) {
    Boat candidate;
    const wxString error = candidate.OpenXML(selected, false, false);
    if (!error.empty() || candidate.Polars.empty()) {
      wxMessageBox(error.empty() ? _("This boat contains no polars.") : error,
                   _("Cannot use boat"), wxOK, this);
      return;
    }
    ApplyBoat(selected);
    return;
  }
  Polar polar;
  wxString error;
  if (!polar.Open(selected, error)) {
    wxMessageBox(error);
    return;
  }
  wxString id = wxString::Format("boat-%lld", wxGetUTCTimeMillis().GetValue());
  while (wxFileExists(root + "boats/" + id + ".xml")) id += "-1";
  const wxString directory = root + "polars/personal/" + id;
  if (!wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    wxMessageBox(_("Unable to create a personal polar directory."));
    return;
  }
  const wxString copy = directory + "/" + wxFileName(selected).GetFullName();
  if (!wxCopyFile(selected, copy, false)) {
    wxMessageBox(_("Unable to copy this polar."));
    return;
  }
  Boat boat;
  boat.Name = DisplayName(wxFileName(selected).GetName());
  polar.FileName = copy;
  polar.Name = boat.Name;
  boat.Polars.push_back(polar);
  const wxString path = root + "boats/" + id + ".xml";
  error = boat.SaveXML(path);
  if (!error.empty()) {
    wxMessageBox(error);
    return;
  }
  ApplyBoat(path);
  routing_.m_ConfigurationDialog.EditBoat();
}

void PassagePanel::EditBoatDetails() {
  if (routing_.m_BoatDialog.IsShown()) {
    routing_.m_BoatDialog.Raise();
    wxMessageBox(
        _("Save or close the polar editor before editing the boat details."),
        _("Boat details"), wxOK, this);
    return;
  }
  auto* route = routing_.FirstCurrentRouteMap();
  if (!route) return;
  const auto path = route->GetConfiguration().boatFileName;
  Boat boat;
  const auto error = boat.OpenXML(path, false, false);
  if (!error.empty() || boat.Polars.empty()) {
    wxMessageBox(error.empty() ? _("Choose a boat with polars first.") : error);
    return;
  }
  wxDialog dialog(this, wxID_ANY, _("Boat and polar details"),
                  wxDefaultPosition, wxSize(660, 590),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* s = new wxBoxSizer(wxVERTICAL);
  Text(&dialog, s,
       _("Optional information describing these polars. Leave unknown "
         "provenance blank. "
         "These descriptions do not certify the data or change the performance "
         "table."));
  auto* grid = new wxFlexGridSizer(2, 8, 8);
  grid->AddGrowableCol(1);
  const auto field = [&](const wxString& label, const wxString& value) {
    grid->Add(new wxStaticText(&dialog, wxID_ANY, label), 0,
              wxALIGN_CENTER_VERTICAL);
    auto* control = new wxTextCtrl(&dialog, wxID_ANY, value);
    control->SetName(label);
    grid->Add(control, 1, wxEXPAND);
    return control;
  };
  auto* name = field(_("Boat name"), boat.Name);
  auto* description = field(_("Boat description"), boat.Description);
  grid->Add(new wxStaticText(&dialog, wxID_ANY, _("Polar")), 0,
            wxALIGN_CENTER_VERTICAL);
  auto* selected = new wxChoice(&dialog, wxID_ANY);
  for (const auto& p : boat.Polars)
    selected->Append(p.Name.empty() ? wxFileName(p.FileName).GetName()
                                    : p.Name);
  grid->Add(selected, 1, wxEXPAND);
  selected->SetSelection(0);
  auto* polarName = field(_("Polar name"), "");
  auto* source = field(_("Source / provenance"), "");
  source->SetToolTip(
      _("For example: published manufacturer data, measured trials, an "
        "estimate, or imported data of unknown origin."));
  auto* sails = field(_("Sail configuration"), "");
  auto* notes = field(_("Notes"), "");
  auto* modified = new wxStaticText(&dialog, wxID_ANY, "");
  s->Add(grid, 1, wxEXPAND | wxALL, 12);
  s->Add(modified, 0, wxEXPAND | wxALL, 12);
  int current = 0;
  const auto store = [&] {
    auto& p = boat.Polars.at(current);
    if (p.Name != polarName->GetValue() || p.Source != source->GetValue() ||
        p.SailConfiguration != sails->GetValue() ||
        p.Notes != notes->GetValue())
      p.Modified =
          wxDateTime::Now().Format("%Y-%m-%dT%H:%M:%SZ", wxDateTime::UTC);
    p.Name = polarName->GetValue();
    p.Source = source->GetValue();
    p.SailConfiguration = sails->GetValue();
    p.Notes = notes->GetValue();
  };
  const auto load = [&] {
    const auto& p = boat.Polars.at(current);
    polarName->SetValue(p.Name);
    source->SetValue(p.Source);
    sails->SetValue(p.SailConfiguration);
    notes->SetValue(p.Notes);
    modified->SetLabel(_("Metadata modified: ") +
                       (p.Modified.empty() ? _("unknown") : p.Modified));
  };
  selected->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) {
    store();
    current = selected->GetSelection();
    load();
  });
  s->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
  dialog.FindWindow(wxID_OK)->SetLabel(_("Save"));
  dialog.SetSizer(s);
  load();
  dialog.CentreOnParent();
  if (dialog.ShowModal() != wxID_OK) return;
  store();
  boat.Name = name->GetValue();
  boat.Description = description->GetValue();
  const auto saved = boat.SaveXML(path);
  if (!saved.empty()) {
    wxMessageBox(saved, _("Cannot save boat details"), wxOK, this);
    return;
  }
  ApplyBoat(path);
}

bool PassagePanel::OpenWeather(const wxString& path, wxString& error) {
  return routing_.OpenPassageForecastForUser(path, error);
}

void PassagePanel::LoadWeather() {
  wxFileDialog dialog(
      this, _("Load passage weather"), wxEmptyString, wxEmptyString,
      _("GRIB files|*.grb;*.grib;*.grb2;*.grib2;*.gz;*.bz2|All files|*.*"),
      wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dialog.ShowModal() != wxID_OK) return;
  wxString error;
  if (!OpenWeather(dialog.GetPath(), error))
    wxMessageBox(error, _("Cannot load weather"), wxOK | wxICON_INFORMATION,
                 this);
  RefreshPassage();
}
void PassagePanel::GenerateWeather() {
  EnsurePassage();
  auto* r = routing_.FirstCurrentRouteMap();
  if (!r) return;
  if (!generator_)
    generator_ =
        new EnvironmentalGribDialog(this, [this](const wxString& path) {
          wxString error;
          if (!OpenWeather(path, error))
            wxMessageBox(error, _("Generated weather"), wxOK, this);
          RefreshPassage();
        });
  const auto c = PlanningConfiguration(r->GetConfiguration());
  if (!generator_->IsShown() && std::isfinite(c.StartLat) &&
      std::isfinite(c.EndLat)) {
    std::vector<RouteWaypointInfo> points;
    wxString error;
    const auto guid =
        c.IsMultiLegGenerated ? c.MultiLegParentRouteGUID : c.RouteGUID;
    if (!guid.empty()) ExtractOpenCPNRouteWaypoints(guid, points, error);
    if (points.size() < 2)
      points = {{c.StartGUID, c.Start, c.StartLat, c.StartLon, 0},
                {c.EndGUID, c.End, c.EndLat, c.EndLon, 1}};
    double distance = 0, south = points.front().lat, north = south;
    double west = points.front().lon, east = west, previous = west;
    for (size_t i = 0; i < points.size(); ++i) {
      const auto& p = points[i];
      double lon = p.lon;
      while (lon - previous > 180) lon -= 360;
      while (lon - previous < -180) lon += 360;
      west = std::min(west, lon);
      east = std::max(east, lon);
      previous = lon;
      south = std::min(south, p.lat);
      north = std::max(north, p.lat);
      if (i)
        distance += DistGreatCircle_Plugin(points[i - 1].lat, points[i - 1].lon,
                                           p.lat, p.lon);
    }
    // Preliminary duration at 4 kn plus 50% and 24 h; actual coverage is
    // rechecked.
    const int estimate =
        std::max(24, int(std::ceil((distance / 4 * 1.5 + 24) / 3)) * 3);
    const int hours = std::min(
        360, c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                 ? std::max(estimate, c.ArrivalSearchHorizonMinutes / 60 + 24)
                 : estimate);
    const double margin = std::max(2.0, std::min(10.0, distance / 120));
    auto wrap = [](double lon) {
      while (lon > 180) lon -= 360;
      while (lon < -180) lon += 360;
      return lon;
    };
    const bool global = east - west + 2 * margin >= 359;
    const wxDateTime start =
        c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
            ? c.PlannedArrivalTime -
                  wxTimeSpan::Minutes(c.ArrivalSearchHorizonMinutes)
            : c.StartTime;
    generator_->SetPassage(
        global ? -180 : wrap(west - margin), std::max(-89.0, south - margin),
        global ? 180 : wrap(east + margin), std::min(89.0, north + margin),
        start, hours, c.Currents, c.MaxSwellMeters > 0);
  }
  generator_->SetDisplayTimeZone(routing_.m_SettingsDialog.UseLocalTimeZone(),
                                  routing_.m_SettingsDialog.DisplayTimeZone());
  generator_->Show();
  generator_->Raise();
}
