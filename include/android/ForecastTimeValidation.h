// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from xGRIB 0.4.5, source a5606ce; scoped to Weather Routing.
#pragma once
#include <cstdint>
#include <string_view>

namespace xgrib {
// Request times are already converted to UTC by each UI. Validate the native
// NWS reader's hourly timestamp contract without rounding the user's choice.
inline bool NonHourlyNwsStart(std::string_view provider,
                             std::int64_t utcSeconds) {
  return provider == "copernicus_nws" && utcSeconds % 3600 != 0;
}
inline constexpr const char* kNwsHourlyStartError =
    "Copernicus NWS currents require a whole UTC hour (minutes and seconds "
    "00). Switch the time display to UTC if necessary and choose a whole-hour "
    "download start. Your requested time has not been changed.";
}  // namespace xgrib
