// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassageReadiness.h"
#include <cmath>
#include <wx/intl.h>

namespace weather_routing {
bool PassageReadiness::CanCalculate() const {
  return FirstBlocker() == nullptr;
}
const RouteDiagnostic* PassageReadiness::FirstBlocker() const {
  for (const auto& section : sections)
    for (const auto& item : section.items)
      if (item.severity == DiagnosticSeverity::Error) return &item;
  return nullptr;
}
unsigned PassageReadiness::Blockers() const {
  unsigned count = 0;
  for (const auto& section : sections)
    count += section.Count(DiagnosticSeverity::Error);
  return count;
}
wxString PassageReadiness::Summary() const {
  if (const auto* blocker = FirstBlocker())
    return _("Cannot calculate: ") + blocker->title;
  return _("Ready to calculate");
}
unsigned PassageReadiness::Warnings() const {
  unsigned count = 0;
  for (const auto& section : sections)
    count += section.Count(DiagnosticSeverity::Warning);
  return count;
}
RouteDiagnosticReport PassageReadiness::Report() const {
  RouteDiagnosticReport result;
  for (const auto& section : sections)
    for (const auto& item : section.items) result.Add(item);
  return result;
}
wxString PassageSectionStatus(const RouteDiagnosticReport& report) {
  if (report.HasErrors()) return _("Setup needed");
  for (const auto& item : report.items)
    if (item.code == "passage.weather.pending") return _("Awaiting passage");
  if (report.Count(DiagnosticSeverity::Warning)) return _("Needs review");
  return _("Ready");
}
PassageAction RecoveryAction(const std::string& code) {
  if (code.find("passage.route.") == 0) return PassageAction::Route;
  if (code.find("passage.boat.") == 0) return PassageAction::Boat;
  if (code.find("passage.limits.") == 0) return PassageAction::Limits;
  return PassageAction::Weather;
}
PassageReadiness EvaluatePassage(const PassageInputs& in) {
  PassageReadiness result;
  const auto add = [&](PassageSection section, const char* code,
                       DiagnosticSeverity severity, const wxString& title,
                       const wxString& detail, const wxString& action) {
    result.sections[static_cast<size_t>(section)].Add(
        {code, severity, title, detail, action, {}, true});
  };
  const auto error = DiagnosticSeverity::Error;
  const auto warning = DiagnosticSeverity::Warning;
  if (!in.positionsValid)
    add(PassageSection::Route, "passage.route.positions", error,
        _("Define the start and destination"),
        _("Choose named positions or waypoints with valid coordinates."),
        _("Edit route"));
  else if (in.savedLegPositionIssue) {
    auto issue = *in.savedLegPositionIssue;
    issue.code = "passage.route.coincident_waypoints";
    result.sections[0].Add(std::move(issue));
  } else if (in.routeWaypoints.size() >= 2) {
    if (auto issue = CheckConsecutiveRouteWaypoints(in.routeWaypoints)) {
      issue->code = "passage.route.coincident_waypoints";
      result.sections[0].Add(*issue);
    }
  } else if (!in.distinctPositions)
    add(PassageSection::Route, "passage.route.same_position", error,
        _("Start and destination must be different"), {}, _("Edit route"));
  if (!in.timeValid)
    add(PassageSection::Route, "passage.route.time", error,
        _("Choose a valid passage time"),
        _("Set a departure or target arrival time in UTC."), _("Edit route"));
  if (!in.routeSequenceValid)
    add(PassageSection::Route, "passage.route.sequence", error,
        _("The OpenCPN route's waypoint sequence has changed"),
        _("Create a fresh passage sequence from that route. Existing results "
          "remain available for comparison."),
        _("Use OpenCPN route"));
  if (!in.boatValid)
    add(PassageSection::Boat, "passage.boat.unusable", error,
        _("Choose a boat with usable polars"), in.boatError,
        _("Choose boat or polar"));
  else if (in.exampleBoat)
    add(PassageSection::Boat, "passage.boat.example", warning,
        _("Example boat selected"),
        _("The 40 ft monohull demonstrates routing. Its performance is not a "
          "measurement of your vessel."),
        _("Choose your boat"));
  if (!in.boatWarnings.empty())
    add(PassageSection::Boat, "passage.boat.warnings", warning,
        _("Review the polar data"), in.boatWarnings,
        _("Review boat and polars"));

  const bool forecast = in.policy == PassageWindPolicy::Forecast ||
                        in.policy == PassageWindPolicy::ForecastWithClimatology;
  const bool climate = in.policy == PassageWindPolicy::Climatology ||
                       in.policy == PassageWindPolicy::ForecastWithClimatology;
  if (!forecast && !climate)
    add(PassageSection::Weather, "passage.weather.source", error,
        _("Select a wind source"),
        _("Current data alone do not supply the wind needed for routing."),
        _("Choose weather policy"));
  const bool fallback = climate && in.climatologyWind;
  if (forecast &&
      (!in.forecastLoaded || (in.coverageChecked && !in.departureWind)))
    add(PassageSection::Weather, "passage.weather.departure",
        fallback ? warning : error,
        in.forecastLoaded ? _("Forecast wind does not cover the departure")
                          : _("No forecast selected"),
        fallback ? _("Climatology will be needed where forecast wind is "
                     "unavailable.")
                 : _("Load or download wind data covering the start and "
                     "passage time, "
                     "or explicitly select Climatology."),
        _("Review weather"));
  if (climate && in.coverageChecked && !in.climatologyWind)
    add(PassageSection::Weather, "passage.weather.climatology",
        forecast && in.departureWind ? warning : error,
        forecast && in.departureWind
            ? _("Forecast wind is available at the departure; Climatology fallback is unavailable here")
            : _("Climatology wind is unavailable at the departure"),
        forecast && in.departureWind
            ? _("Calculation can start using forecast wind. Coverage along the passage is checked during routing; Climatology cannot supply missing wind here.")
            : _("Choose a forecast covering the passage or review the position."),
        _("Review weather"));
  if (forecast && in.departureWind && !in.destinationWind)
    add(PassageSection::Weather, "passage.weather.destination", warning,
        _("Check forecast coverage near the destination"),
        _("The departure-time wind data do not cover that position. Later "
          "coverage and detours are checked during calculation."),
        _("Download passage weather"));
  if (forecast && in.forecastEnd.IsValid() && in.estimatedFinish.IsValid() &&
      in.forecastEnd < in.estimatedFinish)
    add(PassageSection::Weather, "passage.weather.horizon", warning,
        _("The passage may extend beyond the forecast"),
        fallback
            ? _("The duration estimate is preliminary. The result will report "
                "where Climatology was actually used.")
            : _("Obtain a longer forecast or choose Climatology fallback. "
                "Actual passage duration depends on the calculated route."),
        _("Review weather"));
  if (!in.coverageChecked)
    add(PassageSection::Weather, "passage.weather.pending",
        DiagnosticSeverity::Notice,
        _("Coverage is checked after the passage position and time are set"),
        {}, {});
  if (in.coverageChecked && in.waveLimit > 0 && !in.wavesAvailable)
    add(PassageSection::Weather, "passage.weather.waves",
        in.requireWaves ? error : warning,
        _("Wave data are unavailable at the departure"),
        in.requireWaves
            ? _("This passage requires wave coverage. Download wave data.")
            : _("The wave limit cannot be checked where wave data are "
                "missing. The report identifies any missing coverage."),
        _("Download passage weather"));
  if (in.coverageChecked && in.useCurrents && !in.currentsAvailable)
    add(PassageSection::Weather, "passage.weather.currents", warning,
        _("Current data are unavailable at the departure"),
        _("Review the current source or download current data. Missing data "
          "are reported during calculation."),
        _("Review weather"));
  const auto windError = CheckPassageSetting(PassageSetting::TrueWind, in.windLimit);
  const auto apparentError = CheckPassageSetting(PassageSetting::ApparentWind, in.apparentWindLimit);
  const auto waveError = CheckPassageSetting(PassageSetting::WaveHeight, in.waveLimit);
  if (!windError.empty() || !apparentError.empty() || !waveError.empty())
    add(PassageSection::Limits, "passage.limits.invalid", error,
        _("Review wind and wave limits"),
        !windError.empty() ? windError : !apparentError.empty() ? apparentError : waveError,
        _("Passage settings"));
  if (auto errorText = CheckMotorSettings(in.useMotor, in.motorThreshold, in.motorSpeed);
      !errorText.empty())
    add(PassageSection::Limits, "passage.limits.motor", error,
        _("Review motoring settings"), errorText, _("Passage settings"));
  if (auto errorText = CheckPassageSetting(PassageSetting::MinimumDepth, in.minimumDepth);
      !errorText.empty())
    add(PassageSection::Limits, "passage.limits.depth", error,
        _("Review the minimum charted depth"), errorText, _("Passage settings"));
  else if (in.minimumDepth > 0 && !in.chartDepthAvailable)
    add(PassageSection::Limits, "passage.limits.depth_host", error,
        _("This OpenCPN cannot enforce a charted-depth limit"),
        _("Use a compatible chart-aware OpenCPN build, or deliberately switch the depth limit off. A shoreline check cannot verify depth."),
        _("Passage settings"));
  if (windError.empty() && apparentError.empty() && waveError.empty() &&
      (in.windLimit == 0 || in.apparentWindLimit == 0 || in.waveLimit == 0))
    add(PassageSection::Limits, "passage.limits.disabled", warning,
        _("A wind or wave limit is off"),
        _("Review the disabled limit for this vessel and passage."),
        _("Passage settings"));
  if (!in.avoidLand)
    add(PassageSection::Limits, "passage.limits.land", warning,
        _("Land avoidance is off"), _("Review this setting for the passage."),
        _("Passage settings"));
  if (in.avoidBoundaries && !in.boundaryProviderAvailable)
    add(PassageSection::Limits, "passage.limits.boundary_provider", error,
        _("Draw exclusion checking is enabled, but OpenCPN Draw is unavailable"),
        _("Turn off Draw exclusions for this passage, or enable a compatible OpenCPN Draw plugin in OpenCPN's plugin settings. Turning off Draw exclusions keeps land avoidance unchanged. Ordinary weather routing does not require Draw."),
        _("Turn off Draw exclusions for this passage"));
  return result;
}
}  // namespace weather_routing
