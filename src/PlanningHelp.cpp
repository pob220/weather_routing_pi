// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlanningHelp.h"
#include "WeatherRoutingWxCompat.h"
#include <wx/dialog.h>
#include <wx/display.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <map>

namespace weather_routing {
namespace {
std::map<wxWindow*, PlanningHelpTopic> helpTopics;
}
wxString PlanningHelpTitle(PlanningHelpTopic topic) {
  switch (topic) {
    case PlanningHelpTopic::Search:
      return _("Routing search");
    case PlanningHelpTopic::Safety:
      return _("Land and depth checks");
    case PlanningHelpTopic::Shoreline:
      return _("Shoreline detail");
    case PlanningHelpTopic::Resources:
      return _("Memory and workers");
    case PlanningHelpTopic::Timing:
      return _("Departure and arrival");
    case PlanningHelpTopic::Performance:
      return _("Vessel performance");
    case PlanningHelpTopic::Weather:
      return _("Weather sources");
    case PlanningHelpTopic::Navigation:
      return _("Planning tools");
  }
  return wxEmptyString;
}
wxString PlanningHelpText(PlanningHelpTopic topic) {
  switch (topic) {
    case PlanningHelpTopic::Search:
      return _(
          "Auto uses the checked engines in order. Quick2 can supply a "
          "retained fallback while later engines search independently with "
          "fresh allowances. Atlas runs after Quick2, before "
          "Standard. All compares the checked engines. Select at least one "
          "engine for each mode. New passages start in Auto with all engines checked in both lists, unless you have saved different choices.\n\n"
          "Time step and heading separation are nominal search settings. Quick "
          "reduces its step near departure and destination; Standard has a "
          "separate adaptive offshore step. Auto permits Professional up to "
          "400% effort. Disabled fields do not apply to the selected "
          "engine.\n\n"
          "Maximum search angle controls headings sampled around the "
          "destination bearing. Maximum diverted course is a separate hard "
          "geometry constraint. A narrow limit can exclude a necessary tack or "
          "detour.\n\n"
          "The optional final-approach recovery is experimental and off by "
          "default. Fastest/Comfort exploration starts after a validated "
          "route; its percentage and seconds cap limit extra work, not the "
          "first route.\n\n"
          "Reset search preset previews the Balanced preset. It preserves "
          "route, boat, weather, safety and memory settings.");
    case PlanningHelpTopic::Safety:
      return _(
          "Detect Land uses the selected GSHHG shoreline unless loaded-chart "
          "checks are both available and required. With both chart options "
          "enabled on a compatible host, charts determine land and depth "
          "safety; shoreline geometry guides the initial search.\n\n"
          "GSHHG alone does not validate reefs or charted depths. A minimum "
          "depth of 0 m switches off only the depth limit. Positive depth "
          "needs Detect Land, both chart options, a compatible host and chart "
          "coverage.\n\n"
          "Land margin is in nautical miles. Coastal departure/arrival access "
          "is local: at most twice the margin from an endpoint, bounded "
          "between 0.5 and 2 NM. Land and minimum-depth checks still apply.\n\n"
          "OpenCPN Draw exclusion areas are optional and off by default. "
          "Ordinary weather routing and shoreline checks work without Draw "
          "or any other plugin. To use areas you created in Draw, enable a "
          "compatible OpenCPN Draw plugin in OpenCPN Options > Plugins, then "
          "enable Use OpenCPN Draw exclusion areas in Passage settings. "
          "Draw availability is shown beside that setting.\n\n"
          "If a saved passage requests Draw when it is unavailable, choose "
          "Turn off Draw exclusions for this passage on the planning screen "
          "or in Passage checks. This affects all linked legs of that "
          "passage and keeps land avoidance, other passages and saved "
          "defaults unchanged.");
    case PlanningHelpTopic::Shoreline:
      return _(
          "Crude, Low and Intermediate GSHHG datasets are bundled. High and "
          "Full can be installed from Shoreline data and then used offline. "
          "Lower resolutions omit small coastal features.\n\n"
          "Quick, Standard and Professional retain their own shoreline "
          "selections. When chart enforcement is active, the separately saved "
          "scouting resolution guides the preliminary search; the loaded "
          "charts remain authoritative for acceptance.");
    case PlanningHelpTopic::Resources:
      return _(
          "The GRIB timeline cache retains interpolated forecast frames. Its "
          "allowance is a maximum, not an immediate allocation. More cache can "
          "reduce repeated work on large forecasts. The effective allowance "
          "adapts to available physical RAM, including during computation; "
          "original forecast resolution is preserved.\n\n"
          "Chart-safety cache is separate. Zero selects its automatic "
          "allowance. Departure-time workers: 0 = Auto; available RAM can "
          "reduce simultaneous routes. These settings do not increase weather "
          "resolution or certify route quality.");
    case PlanningHelpTopic::Timing:
      return _(
          "Departure mode starts from the configured time. Arrival mode "
          "searches departures before the requested arrival deadline and then "
          "validates the forward route.\n\n"
          "Departure optimisation tests a window around the nominal departure. "
          "Arrival mode instead uses a search horizon before arrival. "
          "Time-zone selection affects display; passage instants remain "
          "unchanged. GRIB Time selects the forecast time.\n\n"
          "For a continuous multi-leg passage, use the sequence calculation so "
          "each leg departs after its predecessor arrives. Separate leg "
          "calculations are independent.");
    case PlanningHelpTopic::Performance:
      return _(
          "Boat and polar library selects or imports a boat. Edit polars opens "
          "the existing boat/polar editor; Boat details edits profile "
          "information. Changes affect the selected passage.\n\n"
          "Wind strength, upwind/downwind factors, night factor and manoeuvre "
          "costs modify the vessel model. 100% is the unadjusted multiplier. "
          "Motoring requires a positive configured motor speed; the threshold "
          "is sailing speed through water. Weather, land and depth limits "
          "still apply when motoring.");
    case PlanningHelpTopic::Weather:
      return _(
          "Forecast only requires forecast wind coverage. Climatology only "
          "uses typical seasonal wind. Forecast with Climatology fallback "
          "permits seasonal wind wherever forecast wind is unavailable. "
          "Climatology is not a forecast.\n\n"
          "Current and wave coverage are separate. Missing waves do not mean "
          "calm seas. Load GRIB opens a file; Download / generate GRIB opens "
          "the integrated generator. Passage checks shows source and coverage "
          "prerequisites.");
    case PlanningHelpTopic::Navigation:
      return _(
          "Plan passage contains Route, Boat and polars, and Weather. Passage "
          "settings contains endpoints, times, limits and weather policy. "
          "Routing controls contains engine selection, search settings, safety "
          "and resource controls.\n\n"
          "Routes and analysis contains the complete route table and its "
          "existing operations. Analysis tools provides direct access to "
          "reports, statistics, plots and run diagnostics. Context-menu "
          "multi-leg creation and sequence optimisation remain available.\n\n"
          "Run diagnostics retains observed progress and final reports for "
          "this session. Observed stage timings are not exact solver timings "
          "or an estimate of time remaining. Export the record for comparisons "
          "made with the same inputs.");
  }
  return wxEmptyString;
}
wxRect VisiblePlanningDialog(wxRect requested, wxSize minimum,
                             const std::vector<wxRect>& workAreas) {
  if (workAreas.empty()) return requested;
  wxRect area = workAreas.front();
  long long best = -1;
  for (const auto& candidate : workAreas) {
    auto overlap = candidate;
    overlap.Intersect(requested);
    const long long score =
        static_cast<long long>(overlap.width) * overlap.height;
    if (score > best) {
      best = score;
      area = candidate;
    }
  }
  const int width =
      std::clamp(requested.width, std::min(minimum.x, area.width), area.width);
  const int height = std::clamp(requested.height,
                                std::min(minimum.y, area.height), area.height);
  return {std::clamp(requested.x, area.x, area.GetRight() - width + 1),
          std::clamp(requested.y, area.y, area.GetBottom() - height + 1), width,
          height};
}
void KeepPlanningDialogVisible(wxWindow* window) {
  std::vector<wxRect> areas;
  for (unsigned i = 0; i < wxDisplay::GetCount(); ++i)
    areas.push_back(wxDisplay(i).GetClientArea());
  if (areas.empty()) areas.push_back(wxGetClientDisplayRect());
  window->SetSize(VisiblePlanningDialog(
      window->GetRect(), WR_FromDIP(window, wxSize(600, 400)), areas));
}
void ShowPlanningHelp(wxWindow* parent, PlanningHelpTopic topic) {
  auto* dialog =
      wxDynamicCast(parent->FindWindow("PlanningContextHelp"), wxDialog);
  if (!dialog) {
    dialog = new wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                          WR_FromDIP(parent, wxSize(580, 440)),
                          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog->SetName("PlanningContextHelp");
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* text = new wxTextCtrl(dialog, wxID_ANY, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
    text->SetName("PlanningContextHelpText");
    sizer->Add(text, 1, wxEXPAND | wxALL, 10);
    auto* close = new wxButton(dialog, wxID_CLOSE, _("Close"));
    sizer->Add(close, 0, wxALIGN_RIGHT | wxALL, 8);
    close->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) { dialog->Hide(); });
    dialog->Bind(wxEVT_CLOSE_WINDOW,
                 [dialog](wxCloseEvent&) { dialog->Hide(); });
    dialog->SetEscapeId(wxID_CLOSE);
    dialog->SetSizer(sizer);
    dialog->CentreOnParent();
  }
  dialog->SetTitle(PlanningHelpTitle(topic));
  auto* text =
      wxDynamicCast(dialog->FindWindow("PlanningContextHelpText"), wxTextCtrl);
  text->ChangeValue(PlanningHelpText(topic));
  text->SetInsertionPoint(0);
  KeepPlanningDialogVisible(dialog);
  dialog->Show();
  dialog->Raise();
  text->SetFocus();
}
wxButton* AddPlanningHelpButton(wxWindow* parent, wxSizer* sizer,
                                PlanningHelpTopic topic) {
  auto* button = new wxButton(parent, wxID_ANY, _("Help"), wxDefaultPosition,
                              wxDefaultSize, wxBU_EXACTFIT);
  button->SetName(PlanningHelpTitle(topic) + _(" help"));
  button->SetToolTip(PlanningHelpTitle(topic));
  button->Bind(wxEVT_BUTTON, [parent, topic](wxCommandEvent&) {
    ShowPlanningHelp(parent, topic);
  });
  sizer->Add(button, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
  return button;
}
void SetPlanningHelp(wxWindow* control, PlanningHelpTopic topic,
                     const wxString& hint) {
  control->SetToolTip(hint);
  if (!helpTopics.contains(control))
    control->Bind(wxEVT_DESTROY, [control](wxWindowDestroyEvent& event) {
      if (event.GetEventObject() == control) helpTopics.erase(control);
      event.Skip();
    });
  helpTopics[control] = topic;
}
void ShowFocusedPlanningHelp(wxWindow* parent, PlanningHelpTopic fallback) {
  for (auto* focus = wxWindow::FindFocus(); focus; focus = focus->GetParent()) {
    const auto found = helpTopics.find(focus);
    if (found != helpTopics.end()) {
      ShowPlanningHelp(parent, found->second);
      return;
    }
  }
  ShowPlanningHelp(parent, fallback);
}
}  // namespace weather_routing
