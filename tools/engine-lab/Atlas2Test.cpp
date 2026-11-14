// SPDX-License-Identifier: GPL-3.0-or-later
// Policy experiment only. The production Atlas source and defaults are frozen.
#include "Fixtures.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace wr = fixture::wr;
wr::RoutingEnvironment environment(const Json::Value& config,
                                   const wr::RoutingRequest& request) {
  wr::RoutingEnvironment env;
  env.grib = std::make_shared<fixture::Weather>(config, request.start);
  env.landAndBoundaries =
      std::make_shared<fixture::Land>(config, request.start);
  return env;
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: atlas2test cases.json");
    std::ifstream input(argv[1]);
    Json::Value cases;
    if (!input || !(input >> cases) || !cases.isArray())
      throw std::runtime_error("Expected a JSON array of cases");
    Json::Value results(Json::arrayValue);
    // Alternate order to reduce systematic warm-cache/CPU-order bias. Providers
    // are fresh for every run; no candidate is shared across the two policies.
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
      for (const auto& config : cases) {
        const auto request = fixture::configure(config);
        for (unsigned slot = 0; slot < 2; ++slot) {
          const bool experimental = (slot + repeat) % 2;
          auto options = fixture::options(config);
          if (experimental) options.improvementAttempts = 0;
          const auto routed = atlas_routing::Engine{}.route(
              request, environment(config, request), options);
          auto fresh = environment(config, request);
          wr::PolarPerformanceModel polar(request.vessel);
          const bool complete =
              routed.route.status == wr::RoutingStatus::Complete;
          const auto replay =
              complete ? wr::RouteValidator{}.validate(request, fresh, polar,
                                                       routed.route.legs)
                       : wr::RouteValidationResult{};
          Json::Value row;
          row["case"] = config["name"];
          row["repeat"] = repeat;
          row["policy"] =
              experimental ? "Atlas2Test-first-valid" : "Atlas-default";
          row["status"] = wr::toString(routed.route.status);
          row["validated"] =
              complete && routed.route.validation.passed && replay.passed;
          row["first_valid_ms"] = routed.atlas.firstValidMs;
          row["total_ms"] = routed.atlas.totalMs;
          row["passage_s"] = Json::Int64(routed.route.metrics.elapsed.count());
          row["geometry_queries"] = Json::UInt64(routed.atlas.geometryQueries);
          row["weather_queries"] = Json::UInt64(routed.atlas.weatherQueries);
          row["edge_attempts"] = Json::UInt64(routed.atlas.edgeAttempts);
          row["independent_failure"] = replay.failureReason;
          results.append(row);
          std::cerr << config["name"].asString() << ' '
                    << row["policy"].asString() << ' '
                    << row["status"].asString() << ' ' << routed.atlas.totalMs
                    << " ms\n";
          if (complete && !row["validated"].asBool())
            throw std::runtime_error("Independent replay failed");
        }
      }
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::cout << Json::writeString(writer, results) << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
