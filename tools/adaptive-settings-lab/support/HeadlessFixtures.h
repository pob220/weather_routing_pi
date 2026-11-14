// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <utility>

#include "Fixtures.h"
#include "supercpn/weather_routing/ResourcePolicy.h"

// Import the older comparison lab's inputs without changing the already frozen
// Atlas fixtures. All seven current solvers use these same providers and request.
namespace headless_fixture {
namespace wr = fixture::wr;

class LabWeather final : public wr::WeatherProvider {
public:
  LabWeather(Json::Value scenario, wr::TimePoint departure)
      : scenario_(std::move(scenario)), departure_(departure) {}
  wr::ParameterCoverage windCoverage() const override {
    wr::ParameterCoverage coverage;
    coverage.available = !scenario_.get("missing_wind", false).asBool();
    coverage.area = {-180, -90, 180, 90};
    coverage.begins = departure_ - std::chrono::hours{24};
    coverage.ends =
        departure_ + std::chrono::hours{scenario_.get("coverage_hours", 960).asInt()};
    return coverage;
  }
  wr::ParameterCoverage currentCoverage() const override {
    auto coverage = windCoverage();
    coverage.available = !scenario_.get("missing_current", false).asBool();
    return coverage;
  }
  wr::ParameterCoverage waveCoverage() const override {
    auto coverage = windCoverage();
    coverage.available = scenario_.isMember("wave_height");
    return coverage;
  }
  wr::WindSample wind(wr::GeoPoint position, wr::TimePoint time) const override {
    if (!windCoverage().available || time > *windCoverage().ends) return {};
    const double hours = (time - departure_).count() / 3600.;
    double speed = scenario_.get("wind_speed", 15.).asDouble();
    double from = scenario_.get("wind_from", 0.).asDouble();
    if (scenario_.get("variable", false).asBool()) {
      speed += 2 * std::sin(hours / 8);
      from += 35 * std::sin(hours / 12 + position.longitude * .2);
    }
    return {true,
            wr::speedDirectionToVector(speed, from + 180),
            {wr::EnvironmentalSource::SyntheticTestField}};
  }
  wr::CurrentSample current(wr::GeoPoint, wr::TimePoint time) const override {
    if (!currentCoverage().available || time > *currentCoverage().ends) return {};
    double speed = scenario_.get("current_speed", 0.).asDouble();
    if (scenario_.get("tidal", false).asBool())
      speed += 3 * std::sin((time - departure_).count() / 3600. * 2 * std::numbers::pi /
                            12.42);
    return {
        true,
        wr::speedDirectionToVector(speed, scenario_.get("current_to", 90.).asDouble()),
        {wr::EnvironmentalSource::SyntheticTestField}};
  }
  wr::WaveSample waves(wr::GeoPoint, wr::TimePoint) const override {
    wr::WaveSample sample;
    sample.available = scenario_.isMember("wave_height");
    sample.significantHeightMetres = scenario_.get("wave_height", 0.).asDouble();
    return sample;
  }
  std::string identity() const override { return "comparison-lab-analytic-field"; }

private:
  Json::Value scenario_;
  wr::TimePoint departure_;
};

class AdjustedWeather final : public wr::WeatherProvider {
public:
  AdjustedWeather(std::shared_ptr<const wr::WeatherProvider> source, double windStrength,
                  bool omitTimeBounds)
      : source_(std::move(source)),
        windStrength_(windStrength),
        omitTimeBounds_(omitTimeBounds) {}
  wr::ParameterCoverage windCoverage() const override {
    return adjustCoverage(source_->windCoverage());
  }
  wr::ParameterCoverage currentCoverage() const override {
    return adjustCoverage(source_->currentCoverage());
  }
  wr::ParameterCoverage waveCoverage() const override { return source_->waveCoverage(); }
  wr::WindSample wind(wr::GeoPoint position, wr::TimePoint time) const override {
    auto sample = source_->wind(position, time);
    sample.velocity.eastKnots *= windStrength_;
    sample.velocity.northKnots *= windStrength_;
    return sample;
  }
  wr::CurrentSample current(wr::GeoPoint position, wr::TimePoint time) const override {
    return source_->current(position, time);
  }
  wr::WaveSample waves(wr::GeoPoint position, wr::TimePoint time) const override {
    return source_->waves(position, time);
  }
  std::string identity() const override { return source_->identity() + "-adjusted"; }

private:
  wr::ParameterCoverage adjustCoverage(wr::ParameterCoverage coverage) const {
    if (omitTimeBounds_) {
      coverage.begins.reset();
      coverage.ends.reset();
    }
    return coverage;
  }
  std::shared_ptr<const wr::WeatherProvider> source_;
  double windStrength_;
  bool omitTimeBounds_;
};

inline void configure(const Json::Value& scenario, wr::RoutingRequest& request) {
  if (!scenario.isMember("headless_suite")) return;
  const bool professional = scenario["headless_suite"] == "professional";
  request.departure = wr::TimePoint{
      wr::Duration{scenario.get("epoch", Json::Int64(1789869600)).asInt64()}};
  request.environment.useWaves = scenario.get("waves", false).asBool();
  request.environment.missingWaves = wr::MissingWavePolicy::DisallowWhenConstrained;
  if (request.environment.useWaves)
    request.constraints.maximumWaveHeightMetres =
        scenario.get("max_wave", 20.).asDouble();
  request.constraints.maximumApparentWindKnots =
      scenario.get("max_apparent_wind", professional ? 50. : 1000.).asDouble();
  request.constraints.maximumLatitudeDegrees =
      std::min(85., scenario.get("max_latitude", 85.).asDouble());
  request.options.minimumTimeStep = wr::Duration{600};
  request.options.headingStepDegrees = scenario.get("heading_step", 10.).asDouble();
  request.options.refinedHeadingStepDegrees =
      std::clamp(request.options.headingStepDegrees / 2., 2.5, 7.5);
  request.options.maximumSearchAngleDegrees =
      scenario.get("native_search_angle", professional ? 120. : 180.).asDouble();
  request.options.destinationToleranceNm = professional ? .25 : .35;
  request.options.spatialCellNm =
      professional ? 2. : std::clamp(request.options.timeStep.count() / 1800., 1., 5.);
  request.options.allowWaiting = scenario.get("anchoring", false).asBool();
  const auto direct = wr::distanceNm(request.start, request.destination);
  request.options.graphCorridorWidthNm =
      professional ? std::clamp(direct * .2, 6., 20.) : 20.;
  request.options.maximumGraphCorridorWidthNm = professional ? INFINITY : 200.;
  request.limits.maximumGeneratedStates =
      scenario.get("max_states", Json::UInt64(4500000)).asUInt64();
  request.limits.maximumRetainedStates = 280000;
  request.limits.maximumGraphLabels = 720000;
  if (professional) {
    request.options.labelsPerCell = 10;
    request.options.retryStages = 7;
    request.options.graphHeadingStepDegrees = request.options.refinedHeadingStepDegrees;
    request.options.routingEffortPercent = scenario.get("effort", 400).asUInt();
    const auto resources = wr::selectRoutingResourcePolicy(
        direct, request.options.routingEffortPercent, false);
    request.limits.maximumCoastalEndpointGeneratedStates =
        resources.maximumCoastalEndpointGeneratedStates;
    request.limits.maximumForwardGeneratedStates =
        resources.maximumForwardGeneratedStates;
    request.limits.maximumForwardArrivalGeneratedStates =
        resources.maximumForwardGeneratedStates / 10;
    request.limits.maximumReverseCandidates = resources.maximumReverseCandidates;
    request.limits.maximumReverseBridgeAttempts = resources.maximumReverseBridgeAttempts;
    request.limits.maximumFrontierRecoveryGeneratedStates =
        resources.maximumFrontierRecoveryGeneratedStates;
    request.limits.maximumFrontierRecoveryLabels =
        resources.maximumFrontierRecoveryLabels;
    request.limits.maximumGraphGeneratedStates = resources.maximumGraphGeneratedStates;
    request.limits.maximumGraphLabels = resources.maximumGraphLabels;
    request.limits.maximumGeneratedStates =
        scenario
            .get("max_states",
                 Json::UInt64(resources.maximumGeneratedStates +
                              request.limits.maximumForwardArrivalGeneratedStates))
            .asUInt64();
    request.limits.maximumRetainedStates =
        scenario.get("retained_states", Json::UInt64(resources.maximumRetainedStates))
            .asUInt64();
    request.limits.maximumExplorationDistanceNm = std::max(500., direct * 5.);
  }
  if (scenario.get("allow_missing_current_waves", false).asBool()) {
    request.environment.missingCurrent = wr::MissingCurrentPolicy::AllowAssumedZero;
    request.environment.zeroCurrentAcknowledged = true;
    request.environment.missingWaves = wr::MissingWavePolicy::AllowWithWarning;
    request.environment.missingWavesAcknowledged = true;
  }
}
}  // namespace headless_fixture
