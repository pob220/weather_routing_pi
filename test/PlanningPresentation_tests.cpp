// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "PlanningHelp.h"
#include "RunDiagnosticsJournal.h"
using namespace weather_routing;

TEST(PlanningGeometry, PreservesPositionOnNegativeCoordinateMonitor) {
  const wxRect desired(-1600, 100, 850, 650);
  EXPECT_EQ(desired, VisiblePlanningDialog(
                         desired, {600, 400},
                         {{0, 0, 1920, 1040}, {-1920, 0, 1920, 1040}}));
}
TEST(PlanningGeometry, RemovedMonitorAndOversizeWindowRemainReachable) {
  const auto rect = VisiblePlanningDialog({3000, 2000, 1800, 1200}, {600, 400},
                                          {{0, 0, 1024, 720}});
  EXPECT_EQ(wxRect(0, 0, 1024, 720), rect);
  EXPECT_EQ(wxRect(20, 30, 480, 320),
            VisiblePlanningDialog({-900, -900, 100, 100}, {600, 400},
                                  {{20, 30, 480, 320}}));
}
TEST(RunDiagnostics,
     ReopeningAndRepeatedFinishDoNotRestartOrExtendElapsedTime) {
  RunDiagnosticsJournal journal;
  const auto start = RunDiagnosticsJournal::Time{};
  journal.Begin("2026-11-13T10:00:00", start);
  journal.Begin("ignored re-open", start + std::chrono::seconds(4));
  journal.Observe("Quick: searching", start + std::chrono::seconds(2));
  journal.Report("Quick: resource_limit_reached\nStandard: complete");
  journal.Finish("Complete", start + std::chrono::seconds(7));
  const auto text = journal.Text(start + std::chrono::seconds(7));
  journal.Finish("incorrect late event", start + std::chrono::seconds(20));
  EXPECT_EQ(text, journal.Text(start + std::chrono::hours(1)));
  EXPECT_TRUE(text.Contains("7.000 s"));
  EXPECT_TRUE(text.Contains("resource_limit_reached"));
  EXPECT_FALSE(text.Contains("ignored re-open"));
}
TEST(RunDiagnostics, InputSnapshotsSurviveEditsAndCanceledRunIsRetained) {
  RunDiagnosticsJournal journal;
  wxString settings = "wind=30; boat=example; selected engines=23";
  journal.Begin("UTC");
  journal.Inputs(settings);
  settings = "wind=0; boat=changed";
  journal.Report("wind_forecast_required");
  journal.Finish("Stopped");
  journal.Begin("next UTC");
  const auto text = journal.Text();
  EXPECT_TRUE(text.Contains("wind=30; boat=example"));
  EXPECT_FALSE(text.Contains("boat=changed"));
  EXPECT_TRUE(text.Contains("Stopped"));
  EXPECT_TRUE(text.Contains("wind_forecast_required"));
  EXPECT_EQ(2u, journal.Size());
}
TEST(RunDiagnostics, BoundsSessionHistoryWithoutLosingStartAndFinalOutcome) {
  RunDiagnosticsJournal journal;
  for (size_t run = 0; run < RunDiagnosticsJournal::maximumRuns + 3; ++run) {
    journal.Begin("UTC");
    journal.Observe("start");
    for (size_t i = 0; i < 200; ++i)
      journal.Observe(wxString::Format("stage-%zu", i));
    journal.Report("Final engine result: cancelled");
    journal.Finish("Stopped");
  }
  const auto text = journal.Text();
  EXPECT_EQ(RunDiagnosticsJournal::maximumRuns, journal.Size());
  EXPECT_TRUE(text.Contains("updates omitted"));
  EXPECT_TRUE(text.Contains("start"));
  EXPECT_TRUE(text.Contains("stage-199"));
  EXPECT_TRUE(text.Contains("Final engine result: cancelled"));
  EXPECT_LT(text.length(), 80000u);
}

TEST(RunDiagnostics, LongMultiLegRunRetainsFirstAndFinalSnapshots) {
  RunDiagnosticsJournal journal;
  journal.Begin("UTC");
  for (unsigned i = 0; i < 40; ++i) {
    journal.Inputs(wxString::Format("input-%u", i));
    journal.Report(wxString::Format("result-%u", i));
  }
  journal.Finish("Complete");
  const auto text = journal.Text();
  EXPECT_TRUE(text.Contains("input-0\n"));
  EXPECT_TRUE(text.Contains("input-39\n"));
  EXPECT_TRUE(text.Contains("result-0\n"));
  EXPECT_TRUE(text.Contains("result-39\n"));
  EXPECT_TRUE(text.Contains("8 intermediate input snapshots omitted"));
  EXPECT_TRUE(text.Contains("8 intermediate reports omitted"));
}
