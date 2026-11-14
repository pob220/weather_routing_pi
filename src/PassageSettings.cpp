// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassageSettings.h"
#include "DepartureScheduler.h"
#include <array>
#include <cmath>
#include <limits>
#include <wx/intl.h>

namespace weather_routing {
namespace {
constexpr double unlimited = std::numeric_limits<double>::infinity();
using Z = ZeroMeaning;
const std::array<PassageSettingDefinition, 14> definitions{{
  {"route.maxTrueWindKnots", wxTRANSLATE("Maximum true wind"), "kn", 0, unlimited, Z::Off,
   wxTRANSLATE("True wind limit in knots. Zero switches this limit off; it does not mean calm conditions.")},
  {"route.maxApparentWindKnots", wxTRANSLATE("Maximum apparent wind"), "kn", 0, unlimited, Z::Off,
   wxTRANSLATE("Apparent wind limit in knots. Zero switches this limit off. Review the limit for your vessel.")},
  {"route.maxSwellMeters", wxTRANSLATE("Maximum wave height"), "m", 0, unlimited, Z::Off,
   wxTRANSLATE("Significant wave-height limit in metres. Zero switches it off. Missing wave data are unknown, not zero.")},
  {"route.headingFromDegrees", wxTRANSLATE("Minimum sailing angle"), "degrees", kSailingAngleMinimum, kSailingAngleMaximum, Z::Value,
   wxTRANSLATE("Minimum angle relative to true wind, from 0 to 180 degrees. This is not a compass bearing. It must not exceed the maximum sailing angle.")},
  {"route.headingToDegrees", wxTRANSLATE("Maximum sailing angle"), "degrees", kSailingAngleMinimum, kSailingAngleMaximum, Z::Value,
   wxTRANSLATE("Maximum angle relative to true wind, from 0 to 180 degrees. The search mirrors these angles onto the other tack where applicable.")},
  {"route.headingStepDegrees", wxTRANSLATE("Sailing-angle increment"), "degrees", kSailingIncrementMinimum, kSailingIncrementMaximum, Z::NotAllowed,
   wxTRANSLATE("Spacing between sailing angles, from 0.1 to 60 degrees. Smaller increments require more calculation. Applies when the selected engine uses this control.")},
  {"route.timeStepSeconds", wxTRANSLATE("Calculation time step"), "s", 1, unlimited, Z::NotAllowed,
   wxTRANSLATE("Stored in seconds; the editor displays hours and minutes. The total must be positive. Each engine may adapt the effective step.")},
  {"route.motorSpeedThresholdKnots", wxTRANSLATE("Motor sailing-speed threshold"), "kn", 0, unlimited, Z::Value,
   wxTRANSLATE("When motoring is enabled, consider the motor below this sailing speed through water, in knots. Inactive while motoring is off.")},
  {"route.motorSpeedKnots", wxTRANSLATE("Motor speed through water"), "kn", 0, unlimited, Z::Value,
   wxTRANSLATE("Constant speed through water in knots while motoring. It must be greater than zero when motoring is enabled. Currents are applied separately.")},
  {"safety.minimumDepthM", wxTRANSLATE("Minimum charted depth"), "m", 0, unlimited, Z::Off,
   wxTRANSLATE("Minimum charted depth in metres. Zero switches the depth constraint off. Positive values require compatible chart-aware OpenCPN and usable charted depth coverage.")},
  {"departureOptimization.concurrentRoutes", wxTRANSLATE("Concurrent departure candidates"), "", 0, kMaximumParallelDepartureCandidates, Z::Auto,
   wxTRANSLATE("Zero means Auto; explicit values are 1 to 12. Auto adapts to the computer. Host services and the global thread limit may reduce actual concurrency.")},
  {"route.upwindEfficiency", wxTRANSLATE("Upwind performance"), "ratio", 0, unlimited, Z::Value,
   wxTRANSLATE("The editor displays a percentage: 100% means the polar's upwind speed. The stored multiplier is 1.0. Existing vessel adjustments are retained.")},
  {"route.downwindEfficiency", wxTRANSLATE("Downwind performance"), "ratio", 0, unlimited, Z::Value,
   wxTRANSLATE("The editor displays a percentage: 100% means the polar's downwind speed. The stored multiplier is 1.0. Existing vessel adjustments are retained.")},
  {"route.nightEfficiency", wxTRANSLATE("Night performance"), "ratio", 0, unlimited, Z::Value,
   wxTRANSLATE("The editor displays a percentage: 100% leaves daytime performance unchanged. The stored multiplier is 1.0 and combines with other performance adjustments.")},
}};
}
const PassageSettingDefinition& DescribePassageSetting(PassageSetting setting) {
  return definitions.at(static_cast<size_t>(setting));
}
wxString PassageSettingHelp(PassageSetting setting) {
  return wxGetTranslation(DescribePassageSetting(setting).help);
}
wxString PassageSettingValue(PassageSetting setting, double value) {
  const auto& spec = DescribePassageSetting(setting);
  if (value == 0 && spec.zero == ZeroMeaning::Off) return _("Off");
  if (value == 0 && spec.zero == ZeroMeaning::Auto) return _("Auto");
  if (wxString::FromUTF8(spec.units) == "ratio")
    return wxString::Format("%g%%", value * 100);
  return wxString::Format("%g", value) +
      (*spec.units ? " " + wxString::FromUTF8(spec.units) : wxString());
}
wxString CheckPassageSetting(PassageSetting setting, double value) {
  const auto& spec = DescribePassageSetting(setting);
  if (std::isfinite(value) && value >= spec.minimum && value <= spec.maximum &&
      (setting != PassageSetting::DepartureWorkers || std::floor(value) == value)) return {};
  return wxGetTranslation(spec.label) + ": " + PassageSettingHelp(setting);
}
wxString CheckMotorSettings(bool enabled, double threshold, double speed) {
  if (!enabled) return {};
  if (auto e = CheckPassageSetting(PassageSetting::MotorThreshold, threshold); !e.empty()) return e;
  if (auto e = CheckPassageSetting(PassageSetting::MotorSpeed, speed); !e.empty()) return e;
  return speed > 0 ? wxString() : _("Set a motor speed through water greater than zero, or switch motoring off.");
}
}  // namespace weather_routing
