// SPDX-License-Identifier: GPL-3.0-or-later
#include "XgribPaths.h"
#include "weather_routing_pi.h"
#include <wx/filename.h>
wxString GetXgribDataDirectory() {
 return GetPluginDataDir(PLUGIN_PACKAGE_NAME)+"/data/";
}
