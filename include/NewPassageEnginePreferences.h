// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RoutingEngineSettingsPersistence.h"

namespace weather_routing {
// New-passage preferences only. Saved route XML, headless requests and the
// engines' own historical defaults keep their existing reader and behaviour.
inline RoutingEngineSettings ReadNewPassageEnginePreferences(
    wxConfigBase* config) {
  auto settings =
      config ? ReadRoutingEngineSettings(*config) : RoutingEngineSettings{};
  ApplyFirstUseEngineDefaults(settings,
                              config && config->GetNumberOfEntries() > 0);
  // Unrelated remembered preferences (for example a boat import) are not an
  // engine choice. Respect each saved selector independently, including zero
  // membership masks and the legacy QuickRoute selection.
  if (!config ||
      (!config->HasEntry("RoutingEngine") && !config->HasEntry("QuickRoute")))
    settings.engine = RoutingEngine::Auto;
  if (!config || !config->HasEntry("AutoEngineMask"))
    settings.autoEngines = kSelectableEnginesMask;
  if (!config || !config->HasEntry("AllEngineMask"))
    settings.allEngines = kSelectableEnginesMask;
  return settings;
}

inline void ApplyNewPassageEngineChoices(RoutingEngineSettings& target,
                                         wxConfigBase* config) {
  const auto preferences = ReadNewPassageEnginePreferences(config);
  target.engine = preferences.engine;
  target.unsupportedId = preferences.unsupportedId;
  target.autoEngines = preferences.autoEngines;
  target.allEngines = preferences.allEngines;
}
}  // namespace weather_routing
