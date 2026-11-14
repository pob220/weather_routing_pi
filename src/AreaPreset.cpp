#include "AreaPreset.h"
#include "environmental_grib/area_validation.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <wx/intl.h>

namespace xgrib {
namespace {

constexpr long kAreaPresetSchemaVersion = 1;
constexpr long kMaximumStoredPresets = 500;
const wxString kAreaPresetConfigPath =
    "/PlugIns/xWeatherRouting2/EnvironmentalGenerator/AreaPresets";

wxString NormalizedName(wxString value) {
  value.Trim(true);
  value.Trim(false);
  return value;
}

wxString Folded(const wxString& value) { return value.Lower(); }

bool HasPresetId(const std::vector<AreaPreset>& presets, const wxString& id) {
  return std::any_of(presets.begin(), presets.end(),
                     [&](const auto& preset) { return preset.id == id; });
}

bool IsKnownProviderId(
    const wxString& id,
    const std::vector<ProviderPreferenceOption>& available_options) {
  if (id.empty()) return true;
  return std::any_of(available_options.begin(), available_options.end(),
                     [&](const auto& option) { return option.id == id; });
}

class ConfigPathGuard {
public:
  explicit ConfigPathGuard(wxConfigBase* config)
      : m_config(config), m_path(config ? config->GetPath() : wxString()) {}
  ~ConfigPathGuard() {
    if (m_config) m_config->SetPath(m_path);
  }

private:
  wxConfigBase* m_config;
  wxString m_path;
};

}  // namespace

std::vector<AreaPreset> SuppliedAreaPresets() {
  return {{"supplied-irish-sea", "Irish Sea / North Channel", -8.5, 50.5, -2.5,
           56.5, "", "copernicus_nws"},
          {"supplied-western-channel", "Western English Channel", -7.0, 48.0,
           -1.0, 51.5, "", "copernicus_nws"},
          {"supplied-north-sea", "North Sea", -5.0, 51.0, 9.0, 60.0, "",
           "copernicus_nws"},
          {"supplied-bay-biscay", "Bay of Biscay", -10.5, 43.0, -1.0, 48.5, "",
           "copernicus_global"},
          {"supplied-florida-straits", "Gulf Stream / Florida Straits", -81.0,
           24.0, -77.0, 28.0, "", "noaa_rtofs"},
          {"supplied-us-east-coast", "US East Coast / Gulf Stream", -81.0, 24.0,
           -70.0, 36.0, "", "noaa_rtofs"},
          {"supplied-caribbean", "Caribbean", -85.0, 10.0, -60.0, 25.0, "",
           "noaa_rtofs"},
          {"supplied-nordic", "Nordic coastal waters", 3.0, 57.0, 12.0, 66.0,
           "metno", "none"}};
}

const std::vector<ProviderPreferenceOption>& WeatherProviderOptions() {
  static const std::vector<ProviderPreferenceOption> options = {
      {"gfs", "NOAA GFS forecast"},
      {"hrrr", "NOAA HRRR 3 km forecast"},
      {"ukv", "Met Office UKV 2 km forecast"},
      {"metno", "MET Norway Nordic 1 km forecast"},
      {"icon_eu", "DWD ICON-EU 7 km forecast"},
      {"ecmwf_ifs", "ECMWF IFS Open Data forecast"},
      {"ecmwf_aifs", "ECMWF AIFS Open Data forecast (experimental)"},
      {"existing", "Existing weather GRIB file"},
      {"none", "None"}};
  return options;
}

const std::vector<ProviderPreferenceOption>& CurrentProviderOptions() {
  static const std::vector<ProviderPreferenceOption> options = {
      {"none", "None"},
      {"existing", "Existing current GRIB file"},
      {"tpxo_cache", "TPXO cache"},
      {"tpxo_direct", "TPXO direct astronomical tide model"},
      {"marine_ie", "Marine.ie Irish Sea latest run"},
      {"copernicus_nws", "Copernicus NWS forecast/model currents"},
      {"copernicus_global", "Copernicus Global forecast/model currents"},
      {"noaa_rtofs", "NOAA RTOFS Global ocean currents"},
      {"noaa_ofs", "NOAA OFS / S-111 coastal currents (experimental)"},
      {"auto", "Auto forecast/model current provider"},
      {"offline_xtd", _("Offline current (.xtd)")},
      {"copernicus_ibi",
       _("Copernicus IBI high-resolution forecast/model currents")},
      {"copernicus_mediterranean",
       _("Copernicus Mediterranean forecast/model currents")}};
  return options;
}

bool NormalizeAreaLongitudes(double* west, double* east) {
  if (!west || !east || !std::isfinite(*west) || !std::isfinite(*east)) return false;
  double span = *east - *west;
  if (span < 0.0 && span > -360.0) span += 360.0;
  if (span <= 0.0 || span > 360.0) return false;
  if (std::abs(span - 360.0) < 1e-8) { *west = -180.0; *east = 180.0; return true; }
  auto normalize = [](double value) {
    double result = std::fmod(value + 180.0, 360.0);
    if (result < 0.0) result += 360.0;
    return result - 180.0;
  };
  *west = normalize(*west);
  *east = normalize(*west + span);
  return true;
}

bool AreaContains(const AreaPreset& coverage, const AreaPreset& requested) {
  double west = coverage.west, east = coverage.east;
  double requestedWest = requested.west, requestedEast = requested.east;
  if (!NormalizeAreaLongitudes(&west, &east) ||
      !NormalizeAreaLongitudes(&requestedWest, &requestedEast)) return false;
  const double width = east > west ? east - west : east + 360.0 - west;
  const double requestedWidth = requestedEast > requestedWest ? requestedEast - requestedWest
                                                               : requestedEast + 360.0 - requestedWest;
  double offset = std::fmod(requestedWest - west, 360.0);
  if (offset < 0.0) offset += 360.0;
  if (offset > 360.0 - 1e-8) offset = 0.0;
  return (width >= 360.0 - 1e-8 || offset + requestedWidth <= width + 1e-8) &&
         requested.south >= coverage.south && requested.north <= coverage.north;
}

wxString ValidateAreaPreset(const AreaPreset& preset) {
  const wxString name = NormalizedName(preset.name);
  if (preset.id.empty()) return "The preset has no internal ID.";
  if (name.empty()) return "Enter a name for the area.";
  const auto issue = environmental_grib::ValidateDownloadArea(
      preset.west, preset.south, preset.east, preset.north);
  if (issue.message) return wxString::FromUTF8(issue.message);
  if (!IsKnownProviderId(preset.weather_provider, WeatherProviderOptions()))
    return "The preferred weather provider is not available.";
  if (!IsKnownProviderId(preset.current_provider, CurrentProviderOptions()))
    return "The preferred current provider is not available.";
  return {};
}

wxString ValidateAreaPresetCollection(const std::vector<AreaPreset>& presets) {
  std::set<wxString> ids;
  std::set<wxString> names;
  for (const auto& preset : presets) {
    const wxString error = ValidateAreaPreset(preset);
    if (!error.empty()) return preset.name + ": " + error;
    if (!ids.insert(preset.id).second)
      return "Two area presets have the same internal ID.";
    const wxString folded = Folded(NormalizedName(preset.name));
    if (!names.insert(folded).second)
      return "Area preset names must be unique.";
  }
  return {};
}

std::vector<AreaPreset> LoadAreaPresets(wxConfigBase* config) {
  if (!config) return SuppliedAreaPresets();
  ConfigPathGuard guard(config);
  config->SetPath(kAreaPresetConfigPath);
  if (!config->HasEntry("schema_version")) return SuppliedAreaPresets();

  const long schema = config->ReadLong("schema_version", 0);
  const long count = config->ReadLong("count", -1);
  if (schema != kAreaPresetSchemaVersion || count < 0 ||
      count > kMaximumStoredPresets)
    return SuppliedAreaPresets();
  if (count == 0) return {};

  std::vector<AreaPreset> loaded;
  loaded.reserve(static_cast<size_t>(count));
  for (long index = 0; index < count; ++index) {
    config->SetPath(kAreaPresetConfigPath +
                    wxString::Format("/Preset%ld", index));
    AreaPreset preset;
    if (!config->Read("id", &preset.id) ||
        !config->Read("name", &preset.name) ||
        !config->Read("west", &preset.west) ||
        !config->Read("south", &preset.south) ||
        !config->Read("east", &preset.east) ||
        !config->Read("north", &preset.north))
      return SuppliedAreaPresets();
    config->Read("weather_provider", &preset.weather_provider, "");
    config->Read("current_provider", &preset.current_provider, "");
    preset.name = NormalizedName(preset.name);
    if (!ValidateAreaPreset(preset).empty()) return SuppliedAreaPresets();
    loaded.push_back(preset);
  }
  if (!ValidateAreaPresetCollection(loaded).empty())
    return SuppliedAreaPresets();
  return loaded;
}

bool SaveAreaPresets(wxConfigBase* config,
                     const std::vector<AreaPreset>& presets, wxString* error) {
  if (error) error->clear();
  const wxString validation = ValidateAreaPresetCollection(presets);
  if (!validation.empty()) {
    if (error) *error = validation;
    return false;
  }
  if (!config) {
    if (error) *error = "OpenCPN configuration is not available.";
    return false;
  }

  ConfigPathGuard guard(config);
  config->SetPath("/PlugIns/xWeatherRouting2/EnvironmentalGenerator");
  config->DeleteGroup("AreaPresets");
  config->SetPath(kAreaPresetConfigPath);
  config->Write("schema_version", kAreaPresetSchemaVersion);
  config->Write("count", static_cast<long>(presets.size()));
  for (size_t index = 0; index < presets.size(); ++index) {
    const auto& preset = presets[index];
    config->SetPath(
        kAreaPresetConfigPath +
        wxString::Format("/Preset%lu", static_cast<unsigned long>(index)));
    config->Write("id", preset.id);
    config->Write("name", NormalizedName(preset.name));
    config->Write("west", preset.west);
    config->Write("south", preset.south);
    config->Write("east", preset.east);
    config->Write("north", preset.north);
    config->Write("weather_provider", preset.weather_provider);
    config->Write("current_provider", preset.current_provider);
  }
  if (!config->Flush()) {
    if (error) *error = "OpenCPN could not save the area presets.";
    return false;
  }
  return true;
}

void RestoreSuppliedAreaPresets(std::vector<AreaPreset>* presets) {
  if (!presets) return;
  const auto supplied = SuppliedAreaPresets();
  std::vector<AreaPreset> userPresets;
  for (const auto& preset : *presets) {
    if (!HasPresetId(supplied, preset.id)) userPresets.push_back(preset);
  }
  *presets = supplied;
  presets->insert(presets->end(), userPresets.begin(), userPresets.end());
}

wxString NextAreaPresetId(const std::vector<AreaPreset>& presets) {
  for (unsigned long sequence = 1;; ++sequence) {
    const wxString candidate = wxString::Format("user-%lu", sequence);
    if (!HasPresetId(presets, candidate)) return candidate;
  }
}

wxString UniqueAreaPresetName(const std::vector<AreaPreset>& presets,
                              const wxString& requested, int ignored_index) {
  wxString base = NormalizedName(requested);
  if (base.empty()) base = "New area";
  auto available = [&](const wxString& candidate) {
    for (size_t index = 0; index < presets.size(); ++index) {
      if (static_cast<int>(index) == ignored_index) continue;
      if (Folded(NormalizedName(presets[index].name)) == Folded(candidate))
        return false;
    }
    return true;
  };
  if (available(base)) return base;
  for (unsigned long sequence = 2;; ++sequence) {
    const wxString candidate = base + wxString::Format(" %lu", sequence);
    if (available(candidate)) return candidate;
  }
}

}  // namespace xgrib
