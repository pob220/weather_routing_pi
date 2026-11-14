// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ClimatologyCompatibility.h"
#include <wx/string.h>

namespace weather_routing {
// Routing-only reader: no overlay factory, dialogs or other plugin required.
class BundledClimatology {
public:
  static void Configure(const wxString& directory);
  static ClimatologyMessage Resolve(const ClimatologyMessage& external = {});
  static wxString Source();
  static bool Data(int setting, const wxDateTime&, double lat, double lon,
                   double& direction, double& speed);
  static bool Atlas(const wxDateTime&, double lat, double lon, int& count,
                    double* directions, double* speeds, double& gale,
                    double& calm);
  static int Cyclones(double lat1, double lon1, double lat2, double lon2,
                      const wxDateTime&, int days);
};
}  // namespace weather_routing
