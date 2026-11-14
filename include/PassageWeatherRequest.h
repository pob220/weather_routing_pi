// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string_view>
#include <wx/datetime.h>

namespace weather_routing {
struct PassageWeatherWindow {
  wxDateTime start;
  int hours;
};
// Download timestamps must match hourly forecast records. Expand the request
// around the passage window; never alter the passage's departure or arrival.
std::optional<PassageWeatherWindow> BuildPassageWeatherWindow(
    const wxDateTime& departure, int hours, int stepHours, bool waves);
bool IsHourlyWeatherTime(const wxDateTime& instant);
// NWS needs exact hourly records; Auto can select NWS. Other readers retain
// their existing timestamp handling, including arbitrary-time tidal models.
std::optional<PassageWeatherWindow> BuildWeatherDownloadWindow(
    const wxDateTime& start, int hours, int stepHours, bool waves,
    std::string_view currentSource);
}  // namespace weather_routing
