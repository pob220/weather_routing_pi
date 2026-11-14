#pragma once

#include <vector>

#include <wx/config.h>
#include <wx/string.h>

namespace xgrib {

struct AreaPreset {
  wxString id;
  wxString name;
  double west{0.0};
  double south{0.0};
  double east{0.0};
  double north{0.0};
  // Empty means that applying the area leaves this provider unchanged.
  wxString weather_provider;
  wxString current_provider;
};

struct ProviderPreferenceOption {
  wxString id;
  wxString label;
};

const std::vector<ProviderPreferenceOption>& WeatherProviderOptions();
const std::vector<ProviderPreferenceOption>& CurrentProviderOptions();

std::vector<AreaPreset> SuppliedAreaPresets();

// Returns an empty string when the preset is valid.
wxString ValidateAreaPreset(const AreaPreset& preset);
// Accept conventional or unwrapped chart bounds; preserve the eastward span.
bool NormalizeAreaLongitudes(double* west, double* east);
bool AreaContains(const AreaPreset& coverage, const AreaPreset& requested);
wxString ValidateAreaPresetCollection(const std::vector<AreaPreset>& presets);

std::vector<AreaPreset> LoadAreaPresets(wxConfigBase* config);
bool SaveAreaPresets(wxConfigBase* config,
                     const std::vector<AreaPreset>& presets,
                     wxString* error = nullptr);

// Restores supplied entries by stable ID without removing user-created areas.
void RestoreSuppliedAreaPresets(std::vector<AreaPreset>* presets);

wxString NextAreaPresetId(const std::vector<AreaPreset>& presets);
wxString UniqueAreaPresetName(const std::vector<AreaPreset>& presets,
                              const wxString& requested,
                              int ignored_index = -1);

}  // namespace xgrib
