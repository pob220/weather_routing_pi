// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/string.h>
#include <chrono>
#include <deque>
#include <vector>

namespace weather_routing {
// Main-thread presentation record only. It observes computations and never
// supplies settings, budgets, candidates or cancellation to a routing engine.
class RunDiagnosticsJournal {
public:
  using Clock = std::chrono::steady_clock;
  using Time = Clock::time_point;
  static constexpr size_t maximumRuns = 8, maximumStages = 120;
  bool Active() const { return !runs_.empty() && !runs_.back().finished; }
  void Begin(const wxString& startedUtc, Time now = Clock::now());
  void Inputs(const wxString& text);
  void Observe(const wxString& stage, Time now = Clock::now());
  void Report(const wxString& text);
  void Finish(const wxString& outcome, Time now = Clock::now());
  wxString Text(Time now = Clock::now()) const;
  size_t Size() const { return runs_.size(); }

private:
  struct Stage {
    double elapsed;
    wxString text;
  };
  struct Run {
    unsigned long long id;
    wxString startedUtc, outcome;
    Time started, ended;
    bool finished = false;
    unsigned omittedStages = 0;
    unsigned omittedInputs = 0, omittedReports = 0;
    std::vector<Stage> stages;
    // wxWidgets exports a std::vector<wxString> specialization on Windows.
    // Keep our private snapshots out of that DLL template specialization.
    std::deque<wxString> inputs, reports;
  };
  static wxString Bounded(wxString text);
  std::deque<Run> runs_;
  unsigned long long next_ = 1;
};
}  // namespace weather_routing
