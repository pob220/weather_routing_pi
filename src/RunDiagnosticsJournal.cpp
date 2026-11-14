// SPDX-License-Identifier: GPL-3.0-or-later
#include "RunDiagnosticsJournal.h"
#include <wx/intl.h>
#include <algorithm>

namespace weather_routing {
wxString RunDiagnosticsJournal::Bounded(wxString text) {
  constexpr size_t limit = 32768;
  if (text.size() > limit) text = text.Left(limit) + _("\n[Record truncated]");
  return text;
}
void RunDiagnosticsJournal::Begin(const wxString& utc, Time now) {
  if (Active()) return;  // A multi-leg action shares one observed run.
  if (runs_.size() == maximumRuns) runs_.pop_front();
  Run run;
  run.id = next_++;
  run.startedUtc = utc;
  run.started = now;
  runs_.push_back(std::move(run));
}
void RunDiagnosticsJournal::Inputs(const wxString& text) {
  if (!Active()) return;
  auto& inputs = runs_.back().inputs;
  const auto bounded = Bounded(text);
  if (std::find(inputs.begin(), inputs.end(), bounded) != inputs.end()) return;
  if (inputs.size() == 32) {
    inputs.erase(inputs.begin() + 1);
    ++runs_.back().omittedInputs;
  }
  inputs.push_back(bounded);
}
void RunDiagnosticsJournal::Observe(const wxString& stage, Time now) {
  if (!Active() || stage.empty()) return;
  auto& run = runs_.back();
  if (!run.stages.empty() && run.stages.back().text == stage) return;
  if (run.stages.size() >= maximumStages) {
    run.stages.erase(run.stages.begin() + 1);
    ++run.omittedStages;
  }
  run.stages.push_back(
      {std::max(0., std::chrono::duration<double>(now - run.started).count()),
       Bounded(stage)});
}
void RunDiagnosticsJournal::Report(const wxString& text) {
  if (!Active()) return;
  auto& reports = runs_.back().reports;
  const auto bounded = Bounded(text);
  if (std::find(reports.begin(), reports.end(), bounded) != reports.end())
    return;
  if (reports.size() == 32) {
    reports.erase(reports.begin() + 1);
    ++runs_.back().omittedReports;
  }
  reports.push_back(bounded);
}
void RunDiagnosticsJournal::Finish(const wxString& outcome, Time now) {
  if (!Active()) return;
  auto& run = runs_.back();
  run.outcome = Bounded(outcome);
  run.ended = now;
  run.finished = true;
}
wxString RunDiagnosticsJournal::Text(Time now) const {
  if (runs_.empty()) return _("No computations recorded in this session.");
  wxString text =
      _("Observed stage times are sampled by the interface, not exact "
        "per-engine execution times or a completion estimate. Final engine "
        "statuses are preserved as reported.\n");
  for (const auto& run : runs_) {
    const double elapsed =
        std::max(0., std::chrono::duration<double>(
                         (run.finished ? run.ended : now) - run.started)
                         .count());
    text += wxString::Format(_(L"\nRun %llu · %s UTC\nElapsed: %.3f s\n"),
                             run.id, run.startedUtc, elapsed);
    if (!run.outcome.empty()) text += run.outcome + "\n";
    if (run.omittedInputs)
      text += wxString::Format(_("%u intermediate input snapshots omitted.\n"),
                               run.omittedInputs);
    for (const auto& input : run.inputs) text += "\n" + input + "\n";
    text += "\n" + _("Observed stages") + "\n";
    if (run.omittedStages)
      text += wxString::Format(_("%u intermediate updates omitted.\n"),
                               run.omittedStages);
    for (const auto& stage : run.stages)
      text += wxString::Format("+%.3f s  %s\n", stage.elapsed, stage.text);
    if (run.omittedReports)
      text += wxString::Format(_("%u intermediate reports omitted.\n"),
                               run.omittedReports);
    for (const auto& report : run.reports) text += "\n" + report + "\n";
  }
  return text;
}
}  // namespace weather_routing
