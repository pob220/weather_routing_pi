// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "PassageReadiness.h"

namespace wr = weather_routing;
namespace {
wr::PassageInputs Ready() {
  wr::PassageInputs input;
  input.positionsValid = input.distinctPositions = input.timeValid = true;
  input.boatValid = input.forecastLoaded = input.departureWind =
      input.destinationWind = true;
  input.wavesAvailable = true;
  return input;
}
TEST(PassageReadiness, ValidInputsPermitCalculationWithoutAdditionalChoices) {
  EXPECT_TRUE(wr::EvaluatePassage(Ready()).CanCalculate());
  EXPECT_EQ(wr::EvaluatePassage(Ready()).Warnings(), 0U);
}
TEST(PassageReadiness, RequiredInputsBlockAndIdentifyTheirRecoveryAction) {
  auto input = Ready();
  input.positionsValid = false;
  auto result = wr::EvaluatePassage(input);
  ASSERT_FALSE(result.CanCalculate());
  ASSERT_FALSE(result.sections[0].items.empty());
  EXPECT_EQ(wr::RecoveryAction(result.sections[0].items[0].code),
            wr::PassageAction::Route);
  input = Ready();
  input.boatValid = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input = Ready();
  input.timeValid = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input = Ready();
  input.distinctPositions = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness, ClimatologyCanWorkWithoutASelectedForecast) {
  auto input = Ready();
  input.forecastLoaded = input.departureWind = false;
  input.policy = wr::PassageWindPolicy::Climatology;
  input.climatologyWind = true;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  input.climatologyWind = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness,
     FallbackIsExplicitAndUnavailableFallbackDoesNotInventWind) {
  auto input = Ready();
  input.forecastLoaded = input.departureWind = false;
  input.climatologyWind = true;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.policy = wr::PassageWindPolicy::ForecastWithClimatology;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  EXPECT_GT(wr::EvaluatePassage(input).Warnings(), 0U);
  input.climatologyWind = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness, MissingWavesWarnOrBlockAccordingToActualPolicy) {
  auto input = Ready();
  input.wavesAvailable = false;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  EXPECT_GT(wr::EvaluatePassage(input).Warnings(), 0U);
  input.requireWaves = true;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.waveLimit = 0;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness,
     IncompleteSpatialAndEstimatedTemporalCoverageAreProvisionalWarnings) {
  auto input = Ready();
  input.destinationWind = false;
  input.forecastEnd = wxDateTime(time_t(1000000));
  input.estimatedFinish = wxDateTime(time_t(1003600));
  auto result = wr::EvaluatePassage(input);
  EXPECT_TRUE(result.CanCalculate());
  EXPECT_EQ(result.Warnings(), 2U);
}
TEST(PassageReadiness, ChangedRouteSequenceRequiresANewLinkedPassage) {
  auto input = Ready();
  input.routeSequenceValid = false;
  auto result = wr::EvaluatePassage(input);
  EXPECT_FALSE(result.CanCalculate());
  EXPECT_EQ(result.sections[0].items[0].code, "passage.route.sequence");
}
TEST(PassageReadiness,
     ExampleBoatAndDisabledLandAvoidanceRemainVisibleWarnings) {
  auto input = Ready();
  input.exampleBoat = true;
  input.avoidLand = false;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_TRUE(result.CanCalculate());
  EXPECT_EQ(result.Warnings(), 2U);
  EXPECT_EQ(wr::PassageSectionStatus(result.sections[1]), "Needs review");
}
}  // namespace

TEST(PassageReadiness,
     UnsetPassageDoesNotMisreportClimatologyCoverageAsMissing) {
  auto input = Ready();
  input.coverageChecked = false;
  input.positionsValid = false;
  input.policy = wr::PassageWindPolicy::Climatology;
  input.climatologyWind = false;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_FALSE(result.CanCalculate());
  EXPECT_FALSE(result.sections[2].HasErrors());
  EXPECT_EQ(wr::PassageSectionStatus(result.sections[2]), "Awaiting passage");
}

TEST(PassageReadiness, NoWindPolicyDoesNotMasqueradeAsClimatology) {
  auto input = Ready();
  input.policy = wr::PassageWindPolicy::None;
  input.climatologyWind = true;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_FALSE(result.CanCalculate());
  EXPECT_EQ(result.sections[2].items[0].code, "passage.weather.source");
}
TEST(PassageReadiness, DisabledLimitsPreserveExistingEngineSemantics) {
  auto input = Ready();
  input.windLimit = input.apparentWindLimit = input.waveLimit = 0;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_TRUE(result.CanCalculate());
  EXPECT_EQ(result.sections[3].items[0].code, "passage.limits.disabled");
  input.windLimit = -1;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness, MotoringRequiresPositiveSpeedOnlyWhenEnabled) {
  auto input = Ready();
  input.motorSpeed = 0;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  input.useMotor = true;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.motorSpeed = 5;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
}
TEST(PassageReadiness, AShorelineOnlyHostCannotEnforceChartedDepth) {
  auto input = Ready();
  input.minimumDepth = 5;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.chartDepthAvailable = true;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  input.minimumDepth = -1;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}

TEST(PassageReadiness, MissingDrawProviderBlocksOnlyExplicitBoundaryAvoidance) {
  auto input = Ready();
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  input.avoidBoundaries = true;
  auto result = wr::EvaluatePassage(input);
  EXPECT_FALSE(result.CanCalculate());
  ASSERT_FALSE(result.sections[3].items.empty());
  EXPECT_EQ(result.sections[3].items[0].code, "passage.limits.boundary_provider");
  EXPECT_EQ(wr::RecoveryAction(result.sections[3].items[0].code),
            wr::PassageAction::Limits);
  EXPECT_TRUE(input.avoidLand);
  EXPECT_TRUE(input.avoidBoundaries);
  input.boundaryProviderAvailable = true;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
}

TEST(PassageReadiness, BlockerSummaryIdentifiesTheEnabledSettingAndDirectRemedy) {
  auto input = Ready();
  input.avoidBoundaries = true;
  input.exampleBoat = true;
  input.wavesAvailable = false;
  const auto result = wr::EvaluatePassage(input);
  ASSERT_NE(result.FirstBlocker(), nullptr);
  EXPECT_EQ(result.Blockers(), 1U);
  EXPECT_EQ(result.Warnings(), 2U);
  EXPECT_TRUE(result.Summary().StartsWith("Cannot calculate: "));
  EXPECT_TRUE(result.Summary().Contains("Draw exclusion checking is enabled"));
  EXPECT_FALSE(result.Summary().Contains("warning"));
  EXPECT_EQ(result.FirstBlocker()->action,
            "Turn off Draw exclusions for this passage");
  EXPECT_TRUE(result.FirstBlocker()->detail.Contains("keeps land avoidance unchanged"));
}

TEST(PassageReadiness, ReadinessWarningsDoNotBecomeCalculationBlockers) {
  auto input = Ready();
  input.policy = wr::PassageWindPolicy::ForecastWithClimatology;
  input.climatologyWind = false;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_EQ(result.FirstBlocker(), nullptr);
  EXPECT_EQ(result.Blockers(), 0U);
  EXPECT_EQ(result.Warnings(), 1U);
  EXPECT_EQ(result.Summary(), "Ready to calculate");
  ASSERT_EQ(result.sections[2].items.size(), 1U);
  EXPECT_TRUE(result.sections[2].items[0].title.Contains(
      "Forecast wind is available at the departure"));
  EXPECT_TRUE(result.sections[2].items[0].detail.Contains(
      "Coverage along the passage is checked during routing"));
}

TEST(PassageReadiness, BlockerCountIncludesProblemsBeyondTheFirstVisibleFix) {
  auto input = Ready();
  input.positionsValid = false;
  input.boatValid = false;
  input.avoidBoundaries = true;
  const auto result = wr::EvaluatePassage(input);
  ASSERT_NE(result.FirstBlocker(), nullptr);
  EXPECT_EQ(result.Blockers(), 3U);
  EXPECT_EQ(result.FirstBlocker()->code, "passage.route.positions");
  EXPECT_FALSE(result.CanCalculate());
}

TEST(PassageReadiness, BundledClimatologyPassageDoesNotRequireOtherPlugins) {
  auto input = Ready();
  input.policy = wr::PassageWindPolicy::Climatology;
  input.forecastLoaded = input.departureWind = input.destinationWind = false;
  input.climatologyWind = true;
  input.wavesAvailable = false;
  input.boundaryProviderAvailable = input.chartDepthAvailable = false;
  const auto result = wr::EvaluatePassage(input);
  EXPECT_TRUE(result.CanCalculate());
  EXPECT_EQ(result.Blockers(), 0U);
  EXPECT_TRUE(input.avoidLand);
  EXPECT_FALSE(input.avoidBoundaries);
}

TEST(PassageReadiness, IntermediateOverlapNamesTheLegAndOffersWaypointEditing) {
  auto input = Ready();
  input.routeWaypoints = {{"a", "001", 50, 0, 1}, {"b", "002", 51, 1, 2},
                          {"c", "003", 51, 1, 3}, {"d", "004", 52, 2, 4}};
  const auto result = wr::EvaluatePassage(input);
  ASSERT_FALSE(result.CanCalculate());
  ASSERT_EQ(result.sections[0].items.size(), 1U);
  const auto issue = result.sections[0].items.front();
  EXPECT_EQ(issue.code, "passage.route.coincident_waypoints");
  EXPECT_EQ(issue.action, "Edit route waypoints");
  EXPECT_TRUE(issue.title.Contains("002"));
  EXPECT_TRUE(issue.title.Contains("003"));
  EXPECT_TRUE(issue.detail.Contains("Leg 2"));
  EXPECT_EQ(wr::RecoveryAction(issue.code), wr::PassageAction::Route);
}
TEST(PassageReadiness, ReturnPassageUsesConsecutiveLegsInsteadOfOverallEndpoints) {
  auto input = Ready();
  input.distinctPositions = false;
  input.routeWaypoints = {{"a", "A", 50, 0, 1}, {"b", "B", 51, 1, 2},
                          {"a", "A", 50, 0, 3}};
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
  input.routeWaypoints[1].lat = 50.000001;
  input.routeWaypoints[1].lon = 0;
  EXPECT_TRUE(wr::EvaluatePassage(input).CanCalculate());
}

TEST(PassageReadiness, SavedLegOverlapIsCheckedIndependentlyOfParentRoute) {
  auto input = Ready();
  input.routeWaypoints = {{"a", "A", 50, 0, 1}, {"b", "B", 51, 1, 2}};
  input.savedLegPositionIssue = wr::CheckRouteLegPositions(
      {"c", "002", 51, 1, 2}, {"d", "003", 51, 1, 3}, 2);
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
  input.routeSequenceValid = false;
  EXPECT_FALSE(wr::EvaluatePassage(input).CanCalculate());
}
