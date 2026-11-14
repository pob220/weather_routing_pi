// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/string.h>

namespace weather_routing {
// UI descriptions of existing settings. Routing engines and scenario parsing
// retain their own behaviour; this module does not change their parameters.
// New-passage defaults never replace omitted JSON values or saved preferences.
struct PassageDefaults {
  static constexpr double windKnots = 30, apparentWindKnots = 40, waveMeters = 3;
  static constexpr double motorThresholdKnots = 2, motorSpeedKnots = 5;
};
inline constexpr double kSailingAngleMinimum = 0, kSailingAngleMaximum = 180;
inline constexpr double kSailingIncrementMinimum = 0.1, kSailingIncrementMaximum = 60;
enum class PassageSetting {
  TrueWind, ApparentWind, WaveHeight, SailingFrom, SailingTo, SailingIncrement,
  TimeStep, MotorThreshold, MotorSpeed, MinimumDepth, DepartureWorkers,
  UpwindFactor, DownwindFactor,
  NightFactor
};
enum class ZeroMeaning { Value, Off, Auto, NotAllowed };
struct PassageSettingDefinition {
  const char* jsonField;
  const char* label;
  const char* units;
  double minimum, maximum;
  ZeroMeaning zero;
  const char* help;
};
const PassageSettingDefinition& DescribePassageSetting(PassageSetting setting);
wxString PassageSettingHelp(PassageSetting setting);
wxString PassageSettingValue(PassageSetting setting, double value);
// Validation uses stored units; an empty string means valid.
wxString CheckPassageSetting(PassageSetting setting, double value);
wxString CheckMotorSettings(bool enabled, double threshold, double speed);
}  // namespace weather_routing
