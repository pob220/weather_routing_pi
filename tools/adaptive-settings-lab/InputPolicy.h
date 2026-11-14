// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Fixtures.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace adaptive_lab {
namespace wr = fixture::wr;

// Hypothesis to qualify, NOT a shipping policy. Only nominal inputs change.
// No knowledge of case names, expected results, or synthetic-weather switches.
inline Json::Value apply(const Json::Value& j, wr::RoutingRequest& r,
                         const wr::RoutingEnvironment& env,
                         const std::string& engine) {
  Json::Value record;
  const auto policy = j.get("input_policy", "manual").asString();
  record["policy"] = policy;
  record["saved_time_step_s"] = Json::Int64(r.options.timeStep.count());
  record["saved_heading_step_deg"] = r.options.headingStepDegrees;
  record["saved_refined_heading_step_deg"] = r.options.refinedHeadingStepDegrees;
  if (policy != "manual" && policy != "duration-coast-v1" && policy != "engine-nominal-v2")
    throw std::runtime_error("Unknown input policy: " + policy);
  if (policy != "manual") {
    const double distance = wr::distanceNm(r.start, r.destination);
    const auto wind = env.grib->wind(r.start, r.departure);
    const auto waves = env.grib->waves(r.start, r.departure);
    const double windSpeed = wind.available ? std::hypot(wind.velocity.eastKnots,
                                                        wind.velocity.northKnots) : 15.;
    const auto best = env.performance->bestSailingSpeedAt(
        r.start, r.departure, windSpeed, waves,
        r.constraints.minimumTrueWindAngleDegrees,
        r.constraints.maximumTrueWindAngleDegrees);
    const double speed = std::clamp(best.value_or(6.) * .7, 1., 25.);
    const double hours = distance / speed;
    // Roughly 120 nominal layers, rounded to a conventional interval.
    // Endpoint/coastal refinement inside each engine remains enabled.
    constexpr std::array steps{600, 900, 1800, 3600, 7200, 10800, 21600};
    const double desired = hours * 3600. / 120.;
    int step = steps.back();
    for (int candidate : steps) if (desired <= candidate) { step = candidate; break; }
    unsigned coastal = 0;
    // Interior probes omit endpoints: a harbour start does not turn an entire
    // ocean crossing into a harbour search. These probes are guidance only;
    // every admitted motion and final route still uses authoritative checks.
    for (int i = 1; i < 16; ++i) {
      const auto point = wr::destinationPoint(r.start,
          wr::initialBearingDegrees(r.start, r.destination), distance * i / 16.);
      if (env.landAndBoundaries->distanceToForbiddenNm(point) < 2.) ++coastal;
    }
    const int cadence = j.get("weather_cadence_s", 0).asInt();
    if (policy == "duration-coast-v1") {
      step = std::min(step, 10800);
      if (coastal) step = std::min(step, 900);
      if (r.environment.useCurrent) step = std::min(step, 1800);
      if (cadence > 0) step = std::min(step, std::max(600, cadence / 2));
    } else {
      // Do not apply a second layer of fine endpoint stepping above engines
      // that already refine it themselves. This is another hypothesis to
      // screen, with the same solver/validation/resource contracts.
      step = std::max(step, 3600);
      if (coastal || r.environment.useCurrent) step = std::min(step, 3600);
      if (cadence > 0) step = std::min(step, std::max(3600, cadence));
    }
    if (engine != "quick" || policy == "duration-coast-v1")
      r.options.timeStep = wr::Duration{step};
    r.options.headingStepDegrees = 10.;
    r.options.refinedHeadingStepDegrees = 5.;
    record["estimated_speed_kn"] = speed;
    record["estimated_passage_hours"] = hours;
    record["interior_coastal_probes"] = coastal;
    record["weather_cadence_s"] = cadence;
    record["standard_keeps_native_stepping"] = engine == "quick" && policy == "engine-nominal-v2";
  }
  record["effective_time_step_s"] = Json::Int64(r.options.timeStep.count());
  record["effective_heading_step_deg"] = r.options.headingStepDegrees;
  record["effective_refined_heading_step_deg"] = r.options.refinedHeadingStepDegrees;
  record["minimum_time_step_s"] = Json::Int64(r.options.minimumTimeStep.count());
  record["internal_adaptive_time"] = r.options.adaptiveTimeStep;
  record["internal_adaptive_headings"] = r.options.adaptiveHeadings;
  return record;
}
} // namespace adaptive_lab
