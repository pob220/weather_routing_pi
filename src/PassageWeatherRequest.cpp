// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassageWeatherRequest.h"
#include <numeric>

namespace weather_routing {
namespace {
time_t SecondsAfterHour(const wxDateTime& instant) {
  return (instant.GetTicks() % 3600 + 3600) % 3600;
}
}
bool IsHourlyWeatherTime(const wxDateTime& instant) {
  return instant.IsValid() && SecondsAfterHour(instant) == 0 &&
         instant.GetMillisecond() == 0;
}
std::optional<PassageWeatherWindow> BuildPassageWeatherWindow(
    const wxDateTime& departure, int hours, int stepHours, bool waves) {
  if (!departure.IsValid() || hours < 1 || hours > 8784 ||
      stepHours < 1 || stepHours > 24) return std::nullopt;
  const auto offset = SecondsAfterHour(departure);
  const int interval = std::lcm(stepHours, waves ? 3 : 1);
  const int coveringHours = hours + ((offset || departure.GetMillisecond()) ? 1 : 0);
  return PassageWeatherWindow{
      wxDateTime(departure.GetTicks() - offset),
      ((coveringHours + interval - 1) / interval) * interval};
}
std::optional<PassageWeatherWindow> BuildWeatherDownloadWindow(
    const wxDateTime& start, int hours, int stepHours, bool waves,
    std::string_view currentSource) {
  if (currentSource == "copernicus_nws" || currentSource == "auto")
    return BuildPassageWeatherWindow(start, hours, stepHours, waves);
  if (!start.IsValid() || hours < 1 || hours > 8784 ||
      stepHours < 1 || stepHours > 24) return std::nullopt;
  return PassageWeatherWindow{start, hours};
}
}  // namespace weather_routing
