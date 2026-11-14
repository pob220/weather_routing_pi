#include "EnvironmentalGribDialog.h"

#include "AreaPresetManagerDialog.h"
#include "TimeZoneDisplay.h"
#include "PlanningTimeCtrl.h"
#include "GeneratorJobJson.h"
#include "ProcessCommand.h"
#include "XgribPaths.h"
#include "PassageWeatherRequest.h"
#include "weather_routing_pi.h"
#include "environmental_grib/area_validation.h"

#include <wx/config.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/choicdlg.h>
#include <wx/process.h>
#include <wx/scrolwin.h>
#include <wx/stdpaths.h>
#include <wx/stream.h>
#include <wx/utils.h>
#include <wx/file.h>

#include "jsonval.h"
#include "jsonreader.h"
#include "jsonwriter.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <initializer_list>
#include <vector>

void EnvironmentalGribDialog::SetPassage(double west,double south,double east,double north,
    const wxDateTime& start,int hours,bool currents,bool waves) {
  m_west->SetValue(wxString::Format("%.3f",west));
  m_south->SetValue(wxString::Format("%.3f",south));
  m_east->SetValue(wxString::Format("%.3f",east));
  m_north->SetValue(wxString::Format("%.3f",north));
  const auto window = weather_routing::BuildPassageWeatherWindow(
      start, hours, m_stepHours->GetValue(), waves);
  if (!window) {
    AppendLog(_("Cannot prefill weather: review the passage time and forecast interval."));
    return;
  }
  m_startUtc->SetUtc(window->start);
  m_durationHours->SetRange(1, std::max(360, window->hours));
  m_durationHours->SetValue(window->hours);
  if (window->start != start || window->hours != hours)
    AppendLog(_("The download window was expanded to whole forecast hours. The passage departure time is unchanged."));
  m_generateWeather->SetValue(true);
  m_generateCurrents->SetValue(currents);
  m_includeWaves->SetValue(waves);
  if(currents) {
    const auto options=xgrib::CurrentProviderOptions();
    for(size_t i=0;i<options.size();++i)
      if(options[i].id=="noaa_rtofs_global")m_currentSource->SetSelection(i);
  }
  UpdateProviderUi();
  QueueEstimate();
}

#ifdef __UNIX__
#include <signal.h>
#endif

namespace {

bool ReadAreaCoordinates(wxTextCtrl* const controls[4], double values[4],
                         wxString* error, wxTextCtrl** invalid = nullptr) {
  for (int i = 0; i < 4; ++i) {
    wxString text = controls[i]->GetValue();
    text.Trim(true).Trim(false);
    const wxString label = wxString::FromUTF8(environmental_grib::AreaCoordinateLabel(
        static_cast<environmental_grib::AreaCoordinate>(i)));
    if (text.empty() || !text.ToDouble(&values[i]) || !std::isfinite(values[i])) {
      if (error) *error = text.empty() ? wxString("Enter ") + label + "."
                                     : label + " must be a finite number.";
      if (invalid) *invalid = controls[i];
      return false;
    }
  }
  const auto issue = environmental_grib::ValidateDownloadArea(
      values[0], values[1], values[2], values[3]);
  if (issue.message) {
    if (error) *error = wxString::FromUTF8(issue.message);
    if (invalid) *invalid = controls[static_cast<int>(issue.field)];
    return false;
  }
  return true;
}

wxString DefaultOutputDirectory() {
  wxFileName path;
  path.AssignDir(weather_routing_pi::StandardPath());
  path.AppendDir("grib");
  path.AppendDir("generated");
  return path.GetPath();
}

wxString TimestampedFilename(const wxString& prefix) {
  return prefix + "_" + wxDateTime::Now().ToUTC().Format("%Y%m%d_%H%M") +
         ".grb";
}

wxDateTime DefaultStartUtc() {
  const auto seconds = wxDateTime::Now().GetTicks();
  return wxDateTime(seconds - seconds % 3600);
}

bool IsMarineIeProvider(const wxString& provider) {
  return provider.Contains("Marine Institute Ireland");
}

wxString MarineIeOutputFilename() {
  return TimestampedFilename("marine_ie_irish_sea_current");
}

wxString DefaultTpxoOutputFilename() {
  return TimestampedFilename("tpxo10_astronomical_tide_current");
}

wxString IrishSeaTpxoOutputFilename() {
  return TimestampedFilename("tpxo10_irish_sea_astronomical_tide_current");
}

wxString DefaultTpxoModelDirectory() {
  wxFileName path(wxGetHomeDir(), "");
  path.AppendDir("OpenCPN");
  path.AppendDir("tide-models");
  return path.GetPath();
}

wxString DefaultTpxoCacheFile() {
  wxFileName path(DefaultOutputDirectory(), "");
  path.AppendDir("tpxo-cache");
  path.SetFullName(
      TimestampedFilename("tpxo10_astronomical_tide_current_cache"));
  path.SetExt("tpxocache");
  return path.GetFullPath();
}

wxString ResolveTpxoAtlasDirectory(const wxString& selected) {
  const auto isAtlas = [](const wxString& directory) {
    wxFileName grid(directory, "grid_tpxo10atlas_v2.nc");
    wxDir model(directory);
    wxString constituent;
    return grid.FileExists() && model.IsOpened() &&
           model.GetFirst(&constituent, "u_*_tpxo10_atlas_30_v2.nc",
                          wxDIR_FILES);
  };
  if (isAtlas(selected)) return selected;
  wxFileName nested(selected, "");
  nested.AppendDir("TPXO10_atlas_v2");
  return isAtlas(nested.GetPath()) ? nested.GetPath() : wxString{};
}

wxString JsonEscape(const wxString& value) {
  wxString escaped;
  for (wxUniChar ch : value) {
    if (ch == '\\') {
      escaped += "\\\\";
    } else if (ch == '"') {
      escaped += "\\\"";
    } else if (ch == '\n') {
      escaped += "\\n";
    } else if (ch == '\r') {
      escaped += "\\r";
    } else if (ch == '\t') {
      escaped += "\\t";
    } else {
      escaped += ch;
    }
  }
  return escaped;
}

bool IsExecutableFile(const wxString& path) {
  return wxFileName::FileExists(path);
}

bool ParseDouble(const wxTextCtrl* control, double* value) {
  return control && control->GetValue().ToDouble(value);
}

bool GribStreamIsStrictlyValid(const wxString& path, wxString* details) {
  wxFile file(path);
  if (!file.IsOpened()) {
    if (details) *details = "could not open output file";
    return false;
  }
  wxFileOffset size = file.Length();
  if (size <= 0) {
    if (details) *details = "output file is empty";
    return false;
  }
  std::vector<unsigned char> buffer(static_cast<size_t>(size));
  wxFileOffset read = file.Read(buffer.data(), buffer.size());
  if (read != size) {
    if (details) *details = "could not read complete output file";
    return false;
  }
  const unsigned char* bytes = buffer.data();
  size_t offset = 0;
  int messages = 0;
  const size_t total = static_cast<size_t>(size);
  while (offset < total) {
    if (offset + 12 > total || bytes[offset] != 'G' ||
        bytes[offset + 1] != 'R' || bytes[offset + 2] != 'I' ||
        bytes[offset + 3] != 'B') {
      if (details)
        *details = wxString::Format("GRIB marker not found at byte offset %zu",
                                    offset);
      return false;
    }
    int edition = bytes[offset + 7];
    size_t messageLength = 0;
    if (edition == 1) {
      messageLength = (static_cast<size_t>(bytes[offset + 4]) << 16) |
                      (static_cast<size_t>(bytes[offset + 5]) << 8) |
                      static_cast<size_t>(bytes[offset + 6]);
    } else if (edition == 2) {
      if (offset + 16 > total) {
        if (details) *details = "truncated GRIB2 header";
        return false;
      }
      for (int i = 8; i < 16; ++i) {
        messageLength =
            (messageLength << 8) | static_cast<size_t>(bytes[offset + i]);
      }
    } else {
      if (details)
        *details = wxString::Format("unsupported GRIB edition %d", edition);
      return false;
    }
    if (messageLength < 12 || offset + messageLength > total) {
      if (details)
        *details = wxString::Format("truncated GRIB message at byte offset %zu",
                                    offset);
      return false;
    }
    size_t end = offset + messageLength - 4;
    if (bytes[end] != '7' || bytes[end + 1] != '7' || bytes[end + 2] != '7' ||
        bytes[end + 3] != '7') {
      if (details)
        *details = wxString::Format(
            "GRIB terminator not found at byte offset %zu", end);
      return false;
    }
    ++messages;
    offset += messageLength;
  }
  if (messages == 0) {
    if (details) *details = "no GRIB messages found";
    return false;
  }
  if (details)
    *details = wxString::Format("validated GRIB stream: %d messages", messages);
  return true;
}

void RedactQueryParameter(wxString* text, const wxString& name) {
  wxString lower = text->Lower();
  wxString needle1 = "?" + name.Lower() + "=";
  wxString needle2 = "&" + name.Lower() + "=";
  for (const auto& needle : {needle1, needle2}) {
    size_t position = lower.find(needle);
    while (position != wxString::npos) {
      size_t value_start = position + needle.Length();
      size_t value_end = value_start;
      while (value_end < text->Length() && (*text)[value_end] != '&' &&
             (*text)[value_end] != '#' && !wxIsspace((*text)[value_end])) {
        ++value_end;
      }
      text->replace(value_start, value_end - value_start, "<redacted>");
      lower = text->Lower();
      position = lower.find(needle, value_start + 10);
    }
  }
}

wxString JoinLines(const wxArrayString& lines) {
  wxString result;
  for (const auto& line : lines) {
    if (!result.empty()) result += "\n";
    result += line;
  }
  return result;
}

wxString ConciseText(wxString text) {
  text.Replace("\r", " ");
  text.Replace("\n", " ");
  text.Replace("\t", " ");
  text.Trim(true).Trim(false);
  while (text.Replace("  ", " ") != 0) {
  }
  if (text.Lower().StartsWith("error:")) {
    text = text.Mid(6);
    text.Trim(false);
  }
  constexpr size_t kMaximumStatusLength = 360;
  if (text.Length() > kMaximumStatusLength) {
    text = text.Left(kMaximumStatusLength - 3) + "...";
  }
  return text;
}

wxString FirstJsonText(const wxJSONValue& object,
                       std::initializer_list<const char*> keys) {
  if (!object.IsObject()) return {};
  for (const char* key : keys) {
    if (!object.HasMember(key)) continue;
    const wxJSONValue value = object.ItemAt(key);
    if (value.IsString() || value.IsInt() || value.IsUInt() || value.IsLong() ||
        value.IsULong() || value.IsDouble()) {
      const wxString text = value.AsString();
      if (!text.empty()) return text;
    }
  }
  return {};
}

bool JsonBoundingBox(const wxJSONValue& root, double* west, double* south,
                     double* east, double* north) {
  if (!root.IsObject()) return false;
  for (const char* key : {"coverage", "bbox"}) {
    if (!root.HasMember(key)) continue;
    const wxJSONValue coverage = root.ItemAt(key);
    if (coverage.IsObject() && coverage.HasMember("west") &&
        coverage.HasMember("south") && coverage.HasMember("east") &&
        coverage.HasMember("north")) {
      return coverage.ItemAt("west").AsString().ToDouble(west) &&
             coverage.ItemAt("south").AsString().ToDouble(south) &&
             coverage.ItemAt("east").AsString().ToDouble(east) &&
             coverage.ItemAt("north").AsString().ToDouble(north);
    }
    if (coverage.IsArray() && coverage.Size() == 4) {
      return coverage.ItemAt(0).AsString().ToDouble(west) &&
             coverage.ItemAt(1).AsString().ToDouble(south) &&
             coverage.ItemAt(2).AsString().ToDouble(east) &&
             coverage.ItemAt(3).AsString().ToDouble(north);
    }
  }
  return false;
}

wxString OfflineTidalFailureStatus(const wxString& detail) {
  const wxString lower = detail.Lower();
  if (lower.Contains("unsupported version") ||
      lower.Contains("unsupported header") ||
      lower.Contains("format_version")) {
    return _("Unsupported XTD version");
  }
  if (lower.Contains("authenticat") || lower.Contains("integrity") ||
      lower.Contains("public-region") || lower.Contains("mac")) {
    return _("Authentication/integrity failure");
  }
  if (lower.Contains("index")) return _("Invalid tile index");
  if (lower.Contains("outside") && lower.Contains("coverage")) {
    return _("Requested area outside coverage");
  }
  if (lower.Contains("metadata") || lower.Contains("constituent") ||
      lower.Contains("velocity_units") || lower.Contains("corrections") ||
      lower.Contains("model data")) {
    return _("Unsupported model data");
  }
  if (lower.Contains("header") || lower.Contains("magic") ||
      lower.Contains("endian") || lower.Contains("invalid xtd package")) {
    return _("Corrupt header");
  }
  return _("Inspection failed");
}

}  // namespace

EnvironmentalGribDialog::EnvironmentalGribDialog(wxWindow* parent,
                                                 GribReadyCallback onGribReady)
    : wxDialog(parent, wxID_ANY, "Environmental GRIB Generator",
               wxDefaultPosition, wxSize(880, 760),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_processTimer(this),
      m_estimateTimer(this),
      m_onGribReady(std::move(onGribReady)) {
  wxLogMessage("xGRIB: constructing environmental generator dialog");
  auto* top = new wxBoxSizer(wxVERTICAL);
  m_scrolled =
      new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxVSCROLL | wxALWAYS_SHOW_SB | wxTAB_TRAVERSAL);
  m_scrolled->SetScrollRate(0, 12);
  m_scrolled->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_ALWAYS);
  m_scrolled->SetMinSize(wxSize(300, 180));
  auto* scrolled = m_scrolled;
  auto* form = new wxBoxSizer(wxVERTICAL);
  auto* grid = new wxFlexGridSizer(2, 8, 8);
  grid->AddGrowableCol(1, 1);

  m_generatorPath = new wxTextCtrl(scrolled, wxID_ANY, FindDefaultGenerator());
  wxLogMessage("xGRIB: environmental generator path resolved");
  m_west = new wxTextCtrl(scrolled, wxID_ANY, "-8.5");
  m_south = new wxTextCtrl(scrolled, wxID_ANY, "50.5");
  m_east = new wxTextCtrl(scrolled, wxID_ANY, "-2.5");
  m_north = new wxTextCtrl(scrolled, wxID_ANY, "56.5");
  auto* areaPresetPanel = new wxPanel(scrolled, wxID_ANY);
  auto* areaPresetSizer = new wxBoxSizer(wxHORIZONTAL);
  m_presetChoice = new wxChoice(areaPresetPanel, wxID_ANY);
  m_manageAreaPresetsButton =
      new wxButton(areaPresetPanel, wxID_ANY, "Manage Areas...");
  areaPresetSizer->Add(m_presetChoice, 1, wxEXPAND | wxRIGHT, 8);
  areaPresetSizer->Add(m_manageAreaPresetsButton, 0);
  areaPresetPanel->SetSizer(areaPresetSizer);
  m_areaPresets = xgrib::LoadAreaPresets(GetOCPNConfigObject());
  PopulateAreaPresetChoice();
  m_startUtc = new PlanningTimeCtrl(scrolled, DefaultStartUtc(), _("GRIB start"));
  m_startUtc->SetToolTip(_("Start of the weather download window. Passage requests start at the preceding whole UTC hour and extend to cover the planned period; the passage departure itself is unchanged."));
  m_durationHours = new wxSpinCtrl(scrolled, wxID_ANY);
  m_durationHours->SetRange(1, 360);
  m_durationHours->SetValue(72);
  m_stepHours = new wxSpinCtrl(scrolled, wxID_ANY);
  m_stepHours->SetRange(1, 24);
  m_stepHours->SetValue(1);
  m_extendForecast = new wxCheckBox(scrolled, wxID_ANY,
                                    "Extend forecast using long-range sources");
  m_extendForecast->SetToolTip(
      "Manually enables selected fallback models. Preferred model records "
      "are retained wherever available; fallback records extend the combined "
      "GRIB to the requested duration.");
  wxString fallbackWeatherProviders[] = {"None", "NOAA GFS forecast"};
  m_fallbackWeatherProvider = new wxChoice(
      scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      WXSIZEOF(fallbackWeatherProviders), fallbackWeatherProviders);
  m_fallbackWeatherProvider->SetSelection(1);
  wxString fallbackWaveProviders[] = {"None", "NOAA GFS Wave"};
  m_fallbackWaveProvider =
      new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                   WXSIZEOF(fallbackWaveProviders), fallbackWaveProviders);
  m_fallbackWaveProvider->SetSelection(1);
  wxString fallbackCurrentSources[] = {"None", _("Offline current (.xtd)")};
  m_fallbackCurrentSource =
      new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                   WXSIZEOF(fallbackCurrentSources), fallbackCurrentSources);
  m_fallbackCurrentSource->SetSelection(1);

  m_generateWeather =
      new wxCheckBox(scrolled, wxID_ANY, "Generate/include weather");
  m_generateWeather->SetValue(true);
  m_weatherProvider = new wxChoice(scrolled, wxID_ANY);
  for (const auto& option : xgrib::WeatherProviderOptions())
    m_weatherProvider->Append(option.label);
  m_weatherProvider->SetSelection(0);
  wxString weatherPresets[] = {"Minimal wind", "Routing", "Marine comfort",
                               "All available display data"};
  m_weatherPreset =
      new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                   WXSIZEOF(weatherPresets), weatherPresets);
  m_weatherPreset->SetSelection(1);
  m_weatherPreset->SetToolTip(
      "Minimal: wind only. Routing: wind, gusts where available, pressure, and air temperature. "
      "Marine: routing fields plus precipitation, cloud cover, and "
      "optional waves. All available: every xGRIB-displayable field supplied "
      "by the selected provider; waves and currents remain separate choices.");
  m_includeWaves = new wxCheckBox(scrolled, wxID_ANY, "Include wave fields");
  m_includeWaves->SetToolTip(
      "Adds significant wave height, primary wave period, and primary wave "
      "direction. NOAA GFS Wave requires no account; Copernicus Global Waves "
      "requires a Copernicus account.");
  wxString waveProviders[] = {"NOAA GFS Wave",
                              "Copernicus Marine Global Waves"};
  m_waveProvider =
      new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                   WXSIZEOF(waveProviders), waveProviders);
  m_waveProvider->SetSelection(0);
  m_waveProvider->SetToolTip(
      "NOAA GFS Wave requires no account. Copernicus Global Waves requires "
      "Copernicus Marine credentials and uses 3-hourly global wave fields.");
  m_existingWeatherFile = new wxFilePickerCtrl(
      scrolled, wxID_ANY, "", "Select weather GRIB", "*.grb;*.grb2");
  m_existingWeatherPath =
      new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxTE_READONLY | wxTE_LEFT);

  m_generateCurrents =
      new wxCheckBox(scrolled, wxID_ANY, "Generate/include currents");
  m_generateCurrents->SetValue(true);
  m_currentSource = new wxChoice(scrolled, wxID_ANY);
  for (const auto& option : xgrib::CurrentProviderOptions())
    m_currentSource->Append(option.label);
  m_currentSource->SetSelection(2);
  m_existingCurrentFile = new wxFilePickerCtrl(
      scrolled, wxID_ANY, "", "Select current GRIB", "*.grb;*.grb2");
  m_existingCurrentPath =
      new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxTE_READONLY | wxTE_LEFT);
  m_offlineTidalFile = new wxFilePickerCtrl(
      scrolled, wxID_ANY, "", _("Select Offline current package"),
      _("xGRIB current data (*.xtd)|*.xtd|All files (*.*)|*.*"),
      wxDefaultPosition, wxDefaultSize, wxFLP_OPEN | wxFLP_FILE_MUST_EXIST);
  m_offlineTidalStatus =
      new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 118),
                     wxTE_MULTILINE | wxTE_READONLY);
  m_offlineTidalStatus->SetMinSize(wxSize(-1, 100));
  wxString offlineModes[] = {_("Astronomical tide only"),
                             _("Tide + expected seasonal circulation")};
  m_offlineCurrentMode =
      new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                   WXSIZEOF(offlineModes), offlineModes);
  m_offlineCurrentMode->SetSelection(0);

  wxString modes[] = {"Forecast/model current GRIB",
                      "Tidal stream prediction from local TPXO model",
                      "Local NetCDF file", "Synthetic source"};
  m_mode = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                        WXSIZEOF(modes), modes);
  m_mode->SetSelection(0);

  wxString providers[] = {
      "Auto", "Copernicus Marine North-West Shelf high-resolution currents",
      "Copernicus Marine Global currents",
      "Marine Institute Ireland Irish Sea currents, 3 day"};
  m_provider = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition,
                            wxDefaultSize, WXSIZEOF(providers), providers);
  m_provider->SetSelection(1);
  m_mode->Hide();
  m_provider->Hide();
  m_mode->SetSize(0, 0);
  m_provider->SetSize(0, 0);
  m_username = new wxTextCtrl(scrolled, wxID_ANY);
  m_password = new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition,
                              wxDefaultSize, wxTE_PASSWORD);
  m_rememberUsername = new wxCheckBox(scrolled, wxID_ANY, "Remember username");
  m_providerNote = new wxStaticText(scrolled, wxID_ANY, "");
  m_tpxoModelDir =
      new wxDirPickerCtrl(scrolled, wxID_ANY, DefaultTpxoModelDirectory());
  m_tpxoModelName = new wxTextCtrl(scrolled, wxID_ANY, "TPXO10-atlas-v2-nc");
  m_tpxoGridSpacing = new wxTextCtrl(scrolled, wxID_ANY, "0.05");
  m_checkTpxoButton = new wxButton(scrolled, wxID_ANY, "Check TPXO model");
  m_useTpxoCache =
      new wxCheckBox(scrolled, wxID_ANY, "Use TPXO cache for this area");
  m_tpxoCacheFile =
      new wxFilePickerCtrl(scrolled, wxID_ANY, DefaultTpxoCacheFile(),
                           "Select TPXO cache file", "*.tpxocache;*.npz");
  m_prepareTpxoCacheButton =
      new wxButton(scrolled, wxID_ANY, "Prepare/update cache");
  m_localNetcdf = new wxFilePickerCtrl(scrolled, wxID_ANY, "",
                                       "Select NetCDF file", "*.nc;*.nc4");
  m_outputDir =
      new wxDirPickerCtrl(scrolled, wxID_ANY, DefaultOutputDirectory());
  m_outputFile = new wxTextCtrl(scrolled, wxID_ANY, "");
  auto* outputBrowse = new wxButton(scrolled, wxID_ANY, "Browse...");
  m_openAfter =
      new wxCheckBox(scrolled, wxID_ANY, "Open generated GRIB after creation");
  m_showMergeInstructions =
      new wxCheckBox(scrolled, wxID_ANY, "Show final GRIB summary");
  m_showMergeInstructions->SetValue(false);
  m_showMergeInstructions->Hide();
  m_showMergeInstructions->SetSize(0, 0);

  auto addRow = [&](const wxString& label, wxWindow* control) -> wxStaticText* {
    auto* labelControl = new wxStaticText(scrolled, wxID_ANY, label);
    grid->Add(labelControl, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(control, 1, wxEXPAND);
    return labelControl;
  };
  // The packaged weather service is selected internally. Development builds
  // can still override it through ENVIRONMENTAL_GRIB_GENERATOR.
  m_generatorPath->Hide();
  addRow("West longitude", m_west);
  addRow("South latitude", m_south);
  addRow("East longitude", m_east);
  addRow("North latitude", m_north);
  addRow("Area preset", areaPresetPanel);
  m_startTimeLabel = addRow(_("Start time"), m_startUtc);
  addRow("Forecast duration (hours)", m_durationHours);
  addRow("Step hours", m_stepHours);
  grid->Add(new wxStaticText(scrolled, wxID_ANY, "Forecast extension"), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_extendForecast, 0);
  m_fallbackWeatherLabel =
      addRow("Long-range weather", m_fallbackWeatherProvider);
  m_fallbackWaveLabel = addRow("Long-range waves", m_fallbackWaveProvider);
  m_fallbackCurrentLabel =
      addRow("Long-range currents", m_fallbackCurrentSource);
  grid->Add(new wxStaticText(scrolled, wxID_ANY, "Weather"), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_generateWeather, 0);
  addRow("Weather provider", m_weatherProvider);
  m_weatherPresetLabel = addRow("Weather preset", m_weatherPreset);
  m_wavesLabel = new wxStaticText(scrolled, wxID_ANY, "Waves");
  grid->Add(m_wavesLabel, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_includeWaves, 0);
  m_waveProviderLabel = addRow("Wave provider", m_waveProvider);
  m_existingWeatherFileLabel =
      addRow("Existing weather GRIB", m_existingWeatherFile);
  m_existingWeatherPathLabel =
      addRow("Selected weather path", m_existingWeatherPath);
  grid->Add(new wxStaticText(scrolled, wxID_ANY, "Currents"), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_generateCurrents, 0);
  addRow("Current source", m_currentSource);
  m_existingCurrentFileLabel =
      addRow("Existing current GRIB", m_existingCurrentFile);
  m_existingCurrentPathLabel =
      addRow("Selected current path", m_existingCurrentPath);
  m_offlineTidalFileLabel =
      addRow(_("Offline current package"), m_offlineTidalFile);
  m_offlineTidalStatusLabel =
      addRow(_("Offline current status"), m_offlineTidalStatus);
  m_offlineCurrentModeLabel =
      addRow(_("Offline current mode"), m_offlineCurrentMode);
  m_usernameLabel = addRow("Copernicus username", m_username);
  m_passwordLabel = addRow("Copernicus password", m_password);
  addRow("Provider note", m_providerNote);
  m_tpxoModelDirLabel = addRow("TPXO model directory", m_tpxoModelDir);
  m_tpxoModelNameLabel = addRow("TPXO model name", m_tpxoModelName);
  m_tpxoGridSpacingLabel =
      addRow("TPXO grid spacing degrees", m_tpxoGridSpacing);
  m_checkTpxoLabel = new wxStaticText(scrolled, wxID_ANY, "TPXO model check");
  grid->Add(m_checkTpxoLabel, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_checkTpxoButton, 0);
  auto* tpxoCacheOptionLabel =
      new wxStaticText(scrolled, wxID_ANY, "TPXO cache");
  tpxoCacheOptionLabel->Hide();
  m_useTpxoCache->Hide();
  grid->Add(tpxoCacheOptionLabel, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_useTpxoCache, 0);
  m_tpxoCacheFileLabel = addRow("TPXO cache file", m_tpxoCacheFile);
  m_prepareTpxoCacheLabel =
      new wxStaticText(scrolled, wxID_ANY, "TPXO cache preparation");
  grid->Add(m_prepareTpxoCacheLabel, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_prepareTpxoCacheButton, 0);
  m_localNetcdfLabel = addRow("Local NetCDF", m_localNetcdf);
  addRow("Output directory", m_outputDir);
  auto* outputFileSizer = new wxBoxSizer(wxHORIZONTAL);
  outputFileSizer->Add(m_outputFile, 1, wxEXPAND | wxRIGHT, 8);
  outputFileSizer->Add(outputBrowse, 0);
  grid->Add(new wxStaticText(scrolled, wxID_ANY, "Output filename"), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(outputFileSizer, 1, wxEXPAND);

  form->Add(grid, 0, wxEXPAND | wxALL, 12);
  m_estimateSummary = new wxStaticText(
      scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
      wxST_NO_AUTORESIZE);
  m_estimateSummary->SetMinSize(wxSize(100, GetCharHeight() * 4));
  form->Add(m_estimateSummary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  ShowEstimate(_("Estimated GRIB file: waiting for settings"),
               _("Decoded forecast data: not yet known"));
  m_scrolled->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
    ShowEstimate(m_estimateFile, m_estimateDecoded);
    event.Skip();
  });
  form->Add(m_rememberUsername, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  form->Add(m_openAfter, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  scrolled->SetSizer(form);
  scrolled->SetVirtualSize(form->GetMinSize());
  top->Add(scrolled, 1, wxEXPAND);

  m_log = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 220),
                         wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
  m_log->SetMinSize(wxSize(300, 100));
  top->Add(m_log, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);
  m_processStatus = new wxStaticText(this, wxID_ANY, _("Ready"));
  top->Add(m_processStatus, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  m_checkButton = new wxButton(this, wxID_ANY, _("Check generator"));
  m_checkButton->SetToolTip(_("Check the installed generator and its supported providers. This does not check live forecast coverage."));
  m_generateButton = new wxButton(this, wxID_OK, "Generate Complete GRIB");
  m_cancelButton = new wxButton(this, wxID_ANY, "Cancel");
  m_closeButton = new wxButton(this, wxID_CANCEL, "Close");
  buttons->Add(m_checkButton, 0, wxRIGHT, 8);
  buttons->AddStretchSpacer(1);
  buttons->Add(m_generateButton, 0, wxRIGHT, 8);
  buttons->Add(m_cancelButton, 0, wxRIGHT, 8);
  buttons->Add(m_closeButton, 0);
  top->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  SetSizerAndFit(top);
  SetMinSize(wxSize(740, 500));
  const wxRect desktop = wxGetClientDisplayRect();
  SetSize(wxSize(std::min(900, desktop.width),
                 std::min(780, desktop.height)));
  CentreOnParent();

  m_checkButton->Bind(wxEVT_BUTTON,
                      &EnvironmentalGribDialog::OnCheckDependencies, this);
  m_generateButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnGenerate,
                         this);
  m_checkTpxoButton->Bind(wxEVT_BUTTON,
                          &EnvironmentalGribDialog::OnCheckTpxoModel, this);
  m_prepareTpxoCacheButton->Bind(
      wxEVT_BUTTON, &EnvironmentalGribDialog::OnPrepareTpxoCache, this);
  outputBrowse->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnBrowseOutput,
                     this);
  m_outputFile->Bind(wxEVT_TEXT,
                     &EnvironmentalGribDialog::OnOutputFilenameChanged, this);
  m_existingWeatherFile->Bind(
      wxEVT_FILEPICKER_CHANGED,
      &EnvironmentalGribDialog::OnExistingGribFileChanged, this);
  m_existingCurrentFile->Bind(
      wxEVT_FILEPICKER_CHANGED,
      &EnvironmentalGribDialog::OnExistingGribFileChanged, this);
  m_offlineTidalFile->Bind(wxEVT_FILEPICKER_CHANGED,
                           &EnvironmentalGribDialog::OnOfflineTidalFileChanged,
                           this);
  m_presetChoice->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnPresetChanged,
                       this);
  m_manageAreaPresetsButton->Bind(
      wxEVT_BUTTON, &EnvironmentalGribDialog::OnManageAreaPresets, this);
  for (wxTextCtrl* field : {m_west, m_south, m_east, m_north}) {
    field->Bind(wxEVT_TEXT, &EnvironmentalGribDialog::OnAreaCoordinateChanged,
                this);
  }
  m_provider->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged,
                   this);
  m_mode->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnModeChanged, this);
  m_weatherProvider->Bind(wxEVT_CHOICE,
                          &EnvironmentalGribDialog::OnProviderChanged, this);
  m_weatherPreset->Bind(wxEVT_CHOICE,
                        &EnvironmentalGribDialog::OnProviderChanged, this);
  m_waveProvider->Bind(wxEVT_CHOICE,
                       &EnvironmentalGribDialog::OnProviderChanged, this);
  m_currentSource->Bind(wxEVT_CHOICE,
                        &EnvironmentalGribDialog::OnProviderChanged, this);
  m_offlineCurrentMode->Bind(wxEVT_CHOICE,
                             &EnvironmentalGribDialog::OnProviderChanged, this);
  m_generateWeather->Bind(wxEVT_CHECKBOX,
                          &EnvironmentalGribDialog::OnProviderChanged, this);
  m_generateCurrents->Bind(wxEVT_CHECKBOX,
                           &EnvironmentalGribDialog::OnProviderChanged, this);
  m_includeWaves->Bind(wxEVT_CHECKBOX,
                       &EnvironmentalGribDialog::OnProviderChanged, this);
  m_extendForecast->Bind(wxEVT_CHECKBOX,
                         &EnvironmentalGribDialog::OnProviderChanged, this);
  m_fallbackWeatherProvider->Bind(
      wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_fallbackWaveProvider->Bind(
      wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_fallbackCurrentSource->Bind(
      wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_cancelButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnCancel, this);
  m_closeButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnClose, this);
  Bind(wxEVT_CLOSE_WINDOW, &EnvironmentalGribDialog::OnDialogClose, this);
  Bind(wxEVT_TIMER, &EnvironmentalGribDialog::OnProcessTimer, this,
        m_processTimer.GetId());
  Bind(wxEVT_TIMER, &EnvironmentalGribDialog::OnEstimateTimer, this,
        m_estimateTimer.GetId());
  Bind(wxEVT_END_PROCESS, &EnvironmentalGribDialog::OnProcessTerminated, this);
  // Existing handlers still run. Also catch fields which previously had no
  // change handler (date, duration, cadence and grid spacing).
  for (wxTextCtrl* field : {m_west, m_south, m_east, m_north,
                           m_tpxoGridSpacing, m_generatorPath})
    field->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
      QueueEstimate(); event.Skip();
    });
  for (wxSpinCtrl* field : {m_durationHours, m_stepHours}) {
    field->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& event) {
      QueueEstimate(); event.Skip();
    });
    field->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
      QueueEstimate(); event.Skip();
    });
  }
  m_startUtc->Bind(wxEVT_PLANNING_TIME_CHANGED, [this](wxCommandEvent&) {
    m_displayTimeZone = m_startUtc->TimeZone();
    m_useLocalTimeZone = m_displayTimeZone != "UTC";
    QueueEstimate();
  });

  AppendLog(
      "Generated environmental GRIBs are model data for planning and "
      "experimentation, not official navigation products.");
  wxLogMessage("xGRIB environmental generator executable: %s",
               m_generatorPath->GetValue());
  LoadSettings();
  wxLogMessage("xGRIB: environmental generator settings loaded");
  ConfigureSmokeTestFromEnvironment();
  UpdateSelectedPathDisplays();
  ValidateOfflineTidalPackage();
  wxLogMessage("xGRIB: environmental generator offline package check complete");
  RefreshOutputFilenameDefault();
  UpdateProviderUi();
  SetBusy(false);
  wxLogMessage("xGRIB: environmental generator dialog ready");
}

void EnvironmentalGribDialog::ConfigureSmokeTestFromEnvironment() {
  wxString enabled;
  if (!wxGetEnv("XGRIB_TEST_OPEN_GENERATOR", &enabled) || enabled != "1") {
    return;
  }

  wxString weatherPath;
  wxString currentPath;
  wxString outputPath;
  wxGetEnv("XGRIB_TEST_WEATHER_FILE", &weatherPath);
  wxGetEnv("XGRIB_TEST_CURRENT_FILE", &currentPath);
  wxGetEnv("XGRIB_TEST_OUTPUT_FILE", &outputPath);
  if (weatherPath.empty() && currentPath.empty()) return;

  m_generateWeather->SetValue(!weatherPath.empty());
  if (!weatherPath.empty()) {
    const int selection =
        m_weatherProvider->FindString("Existing weather GRIB file");
    if (selection != wxNOT_FOUND) m_weatherProvider->SetSelection(selection);
    m_existingWeatherFile->SetPath(weatherPath);
    AppendLog("Smoke test accepted weather GRIB: " + weatherPath);
    wxLogMessage("xGRIB smoke test accepted weather path: %s", weatherPath);
  }

  m_generateCurrents->SetValue(!currentPath.empty());
  if (!currentPath.empty()) {
    const int selection =
        m_currentSource->FindString("Existing current GRIB file");
    if (selection != wxNOT_FOUND) m_currentSource->SetSelection(selection);
    m_existingCurrentFile->SetPath(currentPath);
    AppendLog("Smoke test accepted current GRIB: " + currentPath);
    wxLogMessage("xGRIB smoke test accepted current path: %s", currentPath);
  }

  m_includeWaves->SetValue(false);
  m_extendForecast->SetValue(false);
  m_openAfter->SetValue(true);
  if (!outputPath.empty()) {
    wxFileName output(outputPath);
    m_outputDir->SetPath(output.GetPath());
    m_updatingOutputFilename = true;
    m_outputFile->SetValue(output.GetFullName());
    m_updatingOutputFilename = false;
    m_outputFileUserCustomized = true;
  }
  m_smokeTestConfigured = true;
}

void EnvironmentalGribDialog::UpdateSelectedPathDisplays() {
  const wxString weatherPath = m_existingWeatherFile->GetPath();
  const wxString currentPath = m_existingCurrentFile->GetPath();
  m_existingWeatherPath->ChangeValue(weatherPath);
  m_existingCurrentPath->ChangeValue(currentPath);
  m_existingWeatherPath->SetToolTip(weatherPath);
  m_existingCurrentPath->SetToolTip(currentPath);
  m_existingWeatherPath->SetInsertionPoint(0);
  m_existingCurrentPath->SetInsertionPoint(0);
}

void EnvironmentalGribDialog::RunConfiguredSmokeTest() {
  if (!m_smokeTestConfigured || m_processRunning) return;
  AppendLog("Starting configured deterministic merge smoke test.");
  wxCommandEvent event;
  OnGenerate(event);
}

EnvironmentalGribDialog::~EnvironmentalGribDialog() {
  PrepareForParentShutdown();
}

void EnvironmentalGribDialog::PrepareForParentShutdown() {
  StopEstimate();
  m_processTimer.Stop();
  if (m_processRunning && m_processPid != 0 && ChildProcessStillExists()) {
    wxKillError error = wxKILL_OK;
    xgrib::TerminateGeneratorProcess(m_processPid, &error);
    wxLogMessage(
        "xGRIB: stopped environmental generator pid=%ld during shutdown "
        "(wxKillError=%d)",
        m_processPid, static_cast<int>(error));
  }
  if (m_process) {
    m_process->Detach();
    m_process = nullptr;
  }
  m_processRunning = false;
  m_processPid = 0;
}

void EnvironmentalGribDialog::SetCurrentViewPort(const PlugIn_ViewPort& vp) {
  m_currentViewPort = vp;
  m_hasCurrentViewPort = vp.bValid;
}

void EnvironmentalGribDialog::OnCheckDependencies(wxCommandEvent&) {
  wxString command =
      xgrib::QuoteProcessArgument(m_generatorPath->GetValue()) + " capabilities";
  AppendLog("Checking native generator capabilities...");
  StartCommand(command, "", false);
}

void EnvironmentalGribDialog::OnCheckTpxoModel(wxCommandEvent&) {
  AppendLog("Checking TPXO model...");
  AppendLog("Source: TPXO10 astronomical tide model");
  const wxString atlas = ResolveTpxoAtlasDirectory(m_tpxoModelDir->GetPath());
  if (!atlas.empty()) {
    AppendLog("TPXO10 model is available: " + atlas);
    wxMessageBox("The TPXO10 model grid and constituent files are available.",
                 "TPXO model available", wxOK | wxICON_INFORMATION, this);
  } else {
    const wxString message =
        "TPXO10 model files were not found. Select either the model parent "
        "directory or the TPXO10_atlas_v2 directory: " +
        m_tpxoModelDir->GetPath();
    AppendLog(message);
    wxMessageBox(message, "TPXO model unavailable", wxOK | wxICON_WARNING,
                 this);
  }
}

void EnvironmentalGribDialog::OnPrepareTpxoCache(wxCommandEvent&) {
  if (!ValidateAreaInput()) return;
  wxFileName cachePath(m_tpxoCacheFile->GetPath());
  if (cachePath.GetFullPath().empty()) {
    wxString message =
        "Choose a TPXO cache file path before preparing the cache.";
    AppendLog(message);
    wxMessageBox(message, "Missing TPXO cache file", wxOK | wxICON_WARNING,
                 this);
    return;
  }
  if (!cachePath.DirExists()) {
    cachePath.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  wxString command =
      xgrib::QuoteProcessArgument(m_generatorPath->GetValue()) +
      " prepare-tpxo-cache --bbox " +
      xgrib::QuoteProcessArgument(m_west->GetValue()) + " " +
      xgrib::QuoteProcessArgument(m_south->GetValue()) + " " +
      xgrib::QuoteProcessArgument(m_east->GetValue()) + " " +
      xgrib::QuoteProcessArgument(m_north->GetValue()) +
      " --grid-spacing-deg " +
      xgrib::QuoteProcessArgument(m_tpxoGridSpacing->GetValue()) +
      " --model-dir " +
      xgrib::QuoteProcessArgument(m_tpxoModelDir->GetPath()) + " --output " +
      xgrib::QuoteProcessArgument(cachePath.GetFullPath()) +
      " --overwrite --verbose";
  AppendLog("Preparing TPXO cache...");
  AppendLog("Source: TPXO10 astronomical tide model");
  AppendLog(
      "TPXO cache files are derived from local licensed TPXO model files. Do "
      "not redistribute unless your TPXO licence permits it.");
  SaveSettings();
  StartCommand(command, "", false);
}

bool EnvironmentalGribDialog::ChooseTimeOptions(const wxString& issue_json) {
  wxJSONValue issue;
  wxJSONReader reader;
  if (reader.Parse(issue_json, &issue) != 0) return false;
  wxString message = issue["message"].AsString();
  auto coverage = issue["coverage"];
  // Show actual field ranges rather than a component union that can hide
  // the field restricting a proposed shared period.
  for (int i = 0; i < coverage.Size(); ++i) {
    auto entry = coverage.ItemAt(i);
    AppendLog(entry["component"].AsString() + " " + entry["field"].AsString() +
              ": " + entry["fromUtc"].AsString() + " to " + entry["throughUtc"].AsString());
  }
  if (issue.HasMember("coverageSummary"))
    message += "\n\nField coverage (UTC):\n" + issue["coverageSummary"].AsString();
  wxArrayString labels;
  auto actions = issue["actions"];
  for (int i = 0; i < actions.Size(); ++i) labels.Add(actions.ItemAt(i)["label"].AsString());
  labels.Add("Change settings");
  wxSingleChoiceDialog dialog(this, message, "Forecast time options", labels);
  // Remedies can shorten a forecast or remove sources. Require an explicit
  // selection rather than defaulting to a destructive change.
  dialog.SetSelection(actions.Size());
  if (dialog.ShowModal() != wxID_OK || dialog.GetSelection() >= actions.Size()) return false;
  auto patch = actions.ItemAt(dialog.GetSelection())["request"];
  if (patch.HasMember("hours")) m_durationHours->SetValue(patch["hours"].AsInt());
  if (patch.HasMember("stepHours")) m_stepHours->SetValue(patch["stepHours"].AsInt());
  if (patch.HasMember("weatherProvider")) m_generateWeather->SetValue(false);
  if (patch.HasMember("currentSource")) m_generateCurrents->SetValue(false);
  if (patch.HasMember("includeWaves")) m_includeWaves->SetValue(patch["includeWaves"].AsBool());
  m_timePolicy = patch.HasMember("timePolicy") ? patch["timePolicy"].AsString() : wxString("review");
  m_timeSettingsIssue.clear();
  AppendLog("Time option selected: " + dialog.GetStringSelection());
  UpdateProviderUi();
  return true;
}

bool EnvironmentalGribDialog::ValidateAreaInput() {
  wxTextCtrl* controls[] = {m_west, m_south, m_east, m_north};
  double values[4]{};
  wxString error;
  wxTextCtrl* invalid = nullptr;
  if (ReadAreaCoordinates(controls, values, &error, &invalid)) return true;
  AppendLog("Invalid download area: " + error);
  wxMessageBox(error + "\n\nEnter the four area coordinates, use the current chart area, or select a saved area preset.",
               "Invalid download area", wxOK | wxICON_WARNING, this);
  if (invalid) { invalid->SetFocus(); invalid->SelectAll(); }
  return false;
}

void EnvironmentalGribDialog::OnGenerate(wxCommandEvent&) {
  struct ResetTimeConsent {
    wxString& policy;
    ~ResetTimeConsent() { policy = "review"; }
  } resetTimeConsent{m_timePolicy};
  if (!ValidateAreaInput()) return;
  wxString preview, inputError;
  if (!BuildGenerateJobText(&preview, &inputError)) {
    AppendLog(inputError);
    wxMessageBox(inputError, _("Review generator settings"),
                 wxOK | wxICON_WARNING, this);
    return;
  }
  const auto window = DownloadWindow();
  if (window && (window->start != DisplayedStartUtc() ||
                 window->hours != m_durationHours->GetValue())) {
    // These are download controls only. Never write passage configuration here.
    m_startUtc->SetUtc(window->start);
    m_durationHours->SetRange(1, std::max(m_durationHours->GetMax(), window->hours));
    m_durationHours->SetValue(window->hours);
    const wxString begin = window->start.Format("%Y-%m-%dT%H:%M:%SZ", wxDateTime::UTC);
    const wxString end = (window->start + wxTimeSpan::Hours(window->hours))
                            .Format("%Y-%m-%dT%H:%M:%SZ", wxDateTime::UTC);
    AppendLog(wxString::Format(_("Weather download window: %s to %s. Passage departure and target arrival are unchanged."), begin, end));
    RefreshOutputFilenameDefault();
    QueueEstimate();
  }

  // Already computed alongside the debounced, offline size estimate. Never
  // wait for this cache: the generation worker repeats validation before HTTP.
  if (m_timeSettingsRevision == m_estimateRevision && !m_timeSettingsIssue.empty()) {
    if (!ChooseTimeOptions(m_timeSettingsIssue)) return;
  }
  int mode = m_mode->GetSelection();
  wxString provider = m_provider->GetStringSelection();
  wxString weatherProvider = m_weatherProvider->GetStringSelection();
  wxString currentSource = m_currentSource->GetStringSelection();
  if (m_generateWeather->GetValue() && weatherProvider.Contains("Existing") &&
      m_existingWeatherFile->GetPath().empty()) {
    wxString message =
        "Select an existing weather GRIB file or choose a generated weather "
        "provider.";
    AppendLog(message);
    wxMessageBox(message, "Missing weather GRIB", wxOK | wxICON_WARNING, this);
    return;
  }
  if (m_generateCurrents->GetValue() && currentSource.Contains("Existing") &&
      m_existingCurrentFile->GetPath().empty()) {
    wxString message =
        "Select an existing current GRIB file or choose TPXO cache/None.";
    AppendLog(message);
    wxMessageBox(message, "Missing current GRIB", wxOK | wxICON_WARNING, this);
    return;
  }
  if (m_generateCurrents->GetValue() && IsOfflineTidalNeeded()) {
    ValidateOfflineTidalPackage();
    if (!m_offlineTidalPackageValid) {
      const wxString message = m_offlineTidalStatus->GetValue();
      AppendLog(_("Offline current package is unavailable: ") + message);
      wxMessageBox(message, _("Offline current package unavailable"),
                   wxOK | wxICON_WARNING, this);
      return;
    }
  }
  if (m_generateCurrents->GetValue() && currentSource.Contains("TPXO cache")) {
    wxString cachePath = m_tpxoCacheFile->GetPath();
    if (cachePath.empty()) {
      cachePath = DefaultTpxoCacheFile();
      m_tpxoCacheFile->SetPath(cachePath);
    }
    if (ResolveTpxoAtlasDirectory(m_tpxoModelDir->GetPath()).empty()) {
      wxString message =
          "Select a valid TPXO model parent or TPXO10_atlas_v2 directory "
          "before TPXO cache generation.";
      AppendLog(message);
      wxMessageBox(message, "Missing TPXO model", wxOK | wxICON_WARNING, this);
      return;
    }
    if (!wxFileName::FileExists(cachePath)) {
      wxString message =
          "No suitable TPXO cache exists for this area/grid/model. "
          "Prepare/update it now? This may take about a minute.";
      int response = wxMessageBox(message, "Prepare TPXO cache",
                                  wxYES_NO | wxICON_QUESTION, this);
      if (response != wxYES) {
        AppendLog("Generation cancelled: TPXO cache preparation was declined.");
        return;
      }
      AppendLog(
          "TPXO cache missing; generation will prepare/update it before "
          "merging.");
    }
  }
  if (m_generateCurrents->GetValue() && currentSource.Contains("TPXO direct") &&
      ResolveTpxoAtlasDirectory(m_tpxoModelDir->GetPath()).empty()) {
    wxString message =
        "Select a valid TPXO model parent or TPXO10_atlas_v2 directory "
        "before direct TPXO generation.";
    AppendLog(message);
    wxMessageBox(message, "Missing TPXO model", wxOK | wxICON_WARNING, this);
    return;
  }
  bool copernicusForecast = NeedsCopernicusCredentials();
  if (copernicusForecast &&
      (m_username->GetValue().empty() || m_password->GetValue().empty())) {
    wxString message =
        "Enter your Copernicus Marine username and password for this "
        "operation. The password is held in memory only and is not passed on "
        "the command line.";
    AppendLog(message);
    wxMessageBox(message, "Missing Copernicus credentials",
                 wxOK | wxICON_WARNING, this);
    return;
  }
  if (copernicusForecast && !ConfirmLargeCopernicusRequest()) {
    AppendLog("Generation cancelled before launch.");
    return;
  }
  if (m_generateWeather->GetValue() &&
      weatherProvider.Contains("Met Office UKV") && !ValidateUkvRequest()) {
    AppendLog("Generation cancelled before launch.");
    return;
  }
  if (m_generateWeather->GetValue() &&
      weatherProvider.Contains("MET Norway") && !ValidateMetNoRequest()) {
    AppendLog("Generation cancelled before launch.");
    return;
  }
  if (m_generateWeather->GetValue() && (weatherProvider.Contains("HRRR") ||
                                        weatherProvider.Contains("ICON-EU"))) {
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;
    bool parsed = m_west->GetValue().ToDouble(&west) &&
                  m_south->GetValue().ToDouble(&south) &&
                  m_east->GetValue().ToDouble(&east) &&
                  m_north->GetValue().ToDouble(&north);
    if (parsed && weatherProvider.Contains("HRRR") &&
        (west > east || west < -130.0 || east > -60.0 || south < 20.0 || north > 55.0)) {
      wxString message =
          "The requested bbox is outside the normal NOAA HRRR contiguous "
          "United States domain. "
          "Choose a US area or select a global/regional provider that covers "
          "this area.";
      AppendLog(message);
      wxMessageBox(message, "HRRR area unavailable", wxOK | wxICON_WARNING,
                   this);
      return;
    }
    if (parsed && weatherProvider.Contains("ICON-EU") &&
        (west > east || west < -32.5 || east > 42.5 || south < 20.0 || north > 72.5)) {
      wxString message =
          "The requested bbox is outside the normal DWD ICON-EU Europe domain. "
          "Choose a European area or select a global provider that covers this "
          "area.";
      AppendLog(message);
      wxMessageBox(message, "ICON-EU area unavailable", wxOK | wxICON_WARNING,
                   this);
      return;
    }
  }
  if (m_generateWeather->GetValue() && weatherProvider.Contains("AIFS")) {
    AppendLog(
        "ECMWF AIFS Open Data files may be large if the helper cannot "
        "spatially crop the request.");
  }
  wxFileName output(OutputPath());
  if (!output.DirExists()) {
    output.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  if (copernicusForecast) {
    wxFileName downloadDir;
    downloadDir.AssignDir(m_outputDir->GetPath());
    downloadDir.AppendDir("currentgrib_downloads");
    if (!downloadDir.DirExists()) {
      downloadDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
  }
  wxString command = BuildGenerateCommand();
  m_timePolicy = "review";  // Consent applies to this job only.
  if (command.empty()) {
    AppendLog(
        "Generation cancelled: native generator job could not be created.");
    return;
  }
  SaveSettings();
  AppendLog("Starting environmental GRIB generation...");
  AppendLog("Source: " + SourceLabel());
  wxString childPassword =
      copernicusForecast ? m_password->GetValue() : wxString();
  StartCommand(command, childPassword, true);
}

void EnvironmentalGribDialog::OnBrowseOutput(wxCommandEvent&) {
  wxFileDialog dialog(
      this, "Choose output GRIB path", m_outputDir->GetPath(),
      m_outputFile->GetValue(),
      "GRIB files (*.grb;*.grib)|*.grb;*.grib|All files (*.*)|*.*",
      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dialog.ShowModal() != wxID_OK) return;
  wxFileName selected(dialog.GetPath());
  m_outputDir->SetPath(selected.GetPath());
  m_outputFileUserCustomized = true;
  m_outputFile->SetValue(selected.GetFullName());
}

void EnvironmentalGribDialog::OnOutputFilenameChanged(wxCommandEvent&) {
  if (m_updatingOutputFilename) return;
  wxString value = m_outputFile->GetValue();
  if (value.empty()) {
    m_outputFileUserCustomized = false;
    RefreshOutputFilenameDefault();
    return;
  }
  if (!m_lastAutoOutputFilename.empty() && value == m_lastAutoOutputFilename) {
    m_outputFileUserCustomized = false;
    return;
  }
  m_outputFileUserCustomized = true;
}

void EnvironmentalGribDialog::OnExistingGribFileChanged(
    wxFileDirPickerEvent& event) {
  UpdateSelectedPathDisplays();
  event.Skip();
}

void EnvironmentalGribDialog::OnOfflineTidalFileChanged(wxFileDirPickerEvent&) {
  ValidateOfflineTidalPackage();
  SaveSettings();
}

void EnvironmentalGribDialog::OnPresetChanged(wxCommandEvent& event) {
  ApplyPreset(event.GetSelection());
  if (IsOfflineTidalNeeded()) ValidateOfflineTidalPackage();
}

void EnvironmentalGribDialog::OnManageAreaPresets(wxCommandEvent&) {
  wxString selectedId;
  const int previousSelection = m_presetChoice->GetSelection();
  if (previousSelection >= 2 &&
      previousSelection - 2 < static_cast<int>(m_areaPresets.size()))
    selectedId = m_areaPresets[static_cast<size_t>(previousSelection - 2)].id;

  AreaPresetManagerDialog dialog(this, m_areaPresets, CurrentAreaBounds(),
                                 CurrentChartBounds(), selectedId);
  if (dialog.ShowModal() != wxID_OK) return;

  wxString error;
  selectedId = dialog.SelectedPresetId();
  const auto updated = dialog.Presets();
  if (!xgrib::SaveAreaPresets(GetOCPNConfigObject(), updated, &error)) {
    wxMessageBox(error, "Could not save area presets", wxOK | wxICON_ERROR,
                 this);
    return;
  }
  m_areaPresets = updated;
  PopulateAreaPresetChoice(selectedId);
  if (m_presetChoice->GetSelection() >= 2)
    ApplyPreset(m_presetChoice->GetSelection());
  AppendLog("Saved area presets.");
}

void EnvironmentalGribDialog::OnAreaCoordinateChanged(wxCommandEvent& event) {
  if (!m_applyingAreaPreset && m_presetChoice->GetSelection() != 0)
    m_presetChoice->SetSelection(0);
  RefreshOutputFilenameDefault();
  event.Skip();
}

void EnvironmentalGribDialog::OnProviderChanged(wxCommandEvent& event) {
  if (event.GetEventObject() == m_currentSource ||
      event.GetEventObject() == m_fallbackCurrentSource ||
      event.GetEventObject() == m_extendForecast) {
    if (IsOfflineTidalNeeded()) ValidateOfflineTidalPackage();
    SaveSettings();
  }
  RefreshOutputFilenameDefault();
  UpdateProviderUi();
}

void EnvironmentalGribDialog::OnModeChanged(wxCommandEvent&) {
  RefreshOutputFilenameDefault();
  UpdateProviderUi();
}

void EnvironmentalGribDialog::PopulateAreaPresetChoice(
    const wxString& selected_id) {
  m_presetChoice->Freeze();
  m_presetChoice->Clear();
  m_presetChoice->Append("Custom bbox");
  m_presetChoice->Append("Current chart area");
  int selection = 0;
  for (size_t index = 0; index < m_areaPresets.size(); ++index) {
    m_presetChoice->Append(m_areaPresets[index].name);
    if (!selected_id.empty() && m_areaPresets[index].id == selected_id)
      selection = static_cast<int>(index) + 2;
  }
  m_presetChoice->SetSelection(selection);
  m_presetChoice->Thaw();
}

xgrib::AreaPreset EnvironmentalGribDialog::CurrentAreaBounds() const {
  xgrib::AreaPreset bounds;
  bounds.id = "current-bounds";
  bounds.name = "New area";
  m_west->GetValue().ToDouble(&bounds.west);
  m_south->GetValue().ToDouble(&bounds.south);
  m_east->GetValue().ToDouble(&bounds.east);
  m_north->GetValue().ToDouble(&bounds.north);
  return bounds;
}

std::optional<xgrib::AreaPreset> EnvironmentalGribDialog::CurrentChartBounds()
    const {
  if (!m_hasCurrentViewPort ||
      m_currentViewPort.lon_min >= m_currentViewPort.lon_max ||
      m_currentViewPort.lat_min >= m_currentViewPort.lat_max)
    return std::nullopt;
  xgrib::AreaPreset bounds;
  bounds.id = "chart-bounds";
  bounds.name = "Chart area";
  bounds.west = m_currentViewPort.lon_min;
  bounds.south = m_currentViewPort.lat_min;
  bounds.east = m_currentViewPort.lon_max;
  bounds.north = m_currentViewPort.lat_max;
  if (!xgrib::ValidateAreaPreset(bounds).empty()) return std::nullopt;
  return bounds;
}

void EnvironmentalGribDialog::ApplyProviderPreference(
    const wxString& provider_id,
    const std::vector<xgrib::ProviderPreferenceOption>& options,
    wxChoice* choice, wxCheckBox* enabled) {
  if (provider_id.empty()) return;
  for (size_t index = 0; index < options.size(); ++index) {
    if (options[index].id != provider_id) continue;
    choice->SetSelection(static_cast<int>(index));
    enabled->SetValue(provider_id != "none");
    return;
  }
}

void EnvironmentalGribDialog::ApplyPreset(int selection) {
  if (selection == 0) {
    RefreshOutputFilenameDefault();
    AppendLog("Using custom bbox.");
    return;
  }
  if (selection == 1) {
    if (!m_hasCurrentViewPort) {
      AppendLog(
          "Current chart area is not available yet; enter bbox manually.");
      wxMessageBox(
          "OpenCPN has not provided a valid chart viewport yet. Pan or zoom "
          "the chart, then try again.",
          "Current chart area unavailable", wxOK | wxICON_INFORMATION, this);
      m_presetChoice->SetSelection(0);
      return;
    }
    double chartWest = m_currentViewPort.lon_min, chartEast = m_currentViewPort.lon_max;
    if (!xgrib::NormalizeAreaLongitudes(&chartWest, &chartEast) ||
        m_currentViewPort.lat_min >= m_currentViewPort.lat_max) {
      AppendLog(
          "Current chart area has invalid bounds; enter "
          "bbox manually.");
      wxMessageBox(
          "The current chart area cannot be converted to a simple "
          "west/south/east/north bbox. Enter bbox manually.",
          "Current chart area unavailable", wxOK | wxICON_INFORMATION, this);
      m_presetChoice->SetSelection(0);
      return;
    }
    m_applyingAreaPreset = true;
    m_west->ChangeValue(wxString::Format("%.6f", chartWest));
    m_south->ChangeValue(wxString::Format("%.6f", m_currentViewPort.lat_min));
    m_east->ChangeValue(wxString::Format("%.6f", chartEast));
    m_north->ChangeValue(wxString::Format("%.6f", m_currentViewPort.lat_max));
    m_applyingAreaPreset = false;
    RefreshOutputFilenameDefault();
    AppendLog("Applied current chart area preset.");
    m_presetChoice->SetSelection(0);
    return;
  }
  int areaIndex = selection - 2;
  if (areaIndex >= 0 && areaIndex < static_cast<int>(m_areaPresets.size())) {
    const xgrib::AreaPreset& area =
        m_areaPresets[static_cast<size_t>(areaIndex)];
    m_applyingAreaPreset = true;
    m_west->ChangeValue(wxString::Format("%.6g", area.west));
    m_south->ChangeValue(wxString::Format("%.6g", area.south));
    m_east->ChangeValue(wxString::Format("%.6g", area.east));
    m_north->ChangeValue(wxString::Format("%.6g", area.north));
    m_applyingAreaPreset = false;
    ApplyProviderPreference(area.weather_provider,
                            xgrib::WeatherProviderOptions(), m_weatherProvider,
                            m_generateWeather);
    ApplyProviderPreference(area.current_provider,
                            xgrib::CurrentProviderOptions(), m_currentSource,
                            m_generateCurrents);
    RefreshOutputFilenameDefault();
    UpdateProviderUi();
    AppendLog("Applied area preset: " + m_presetChoice->GetString(selection));
  }
}

bool EnvironmentalGribDialog::AutoWouldUseMarineIe() const {
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  bool parsed = m_west->GetValue().ToDouble(&west) &&
                m_south->GetValue().ToDouble(&south) &&
                m_east->GetValue().ToDouble(&east) &&
                m_north->GetValue().ToDouble(&north);
  return parsed && west < east && west >= -6.994 && east <= -4.006 && south >= 51.506 &&
         north <= 55.494 && m_durationHours->GetValue() <= 72;
}

bool EnvironmentalGribDialog::NeedsCopernicusCredentials() const {
  bool waveCopernicus =
      m_includeWaves->GetValue() &&
      m_waveProvider->GetStringSelection().Contains("Copernicus");
  if (waveCopernicus) {
    return true;
  }
  if (!m_generateCurrents->GetValue()) {
    return false;
  }
  wxString currentSource = m_currentSource->GetStringSelection();
  return currentSource.Contains("Copernicus") || currentSource.Contains("Auto");
}

bool EnvironmentalGribDialog::IsOfflineTidalSelected() const {
  return m_currentSource &&
         m_currentSource->GetStringSelection() == _("Offline current (.xtd)");
}

bool EnvironmentalGribDialog::IsOfflineTidalNeeded() const {
  if (IsOfflineTidalSelected()) return true;
  return m_extendForecast && m_extendForecast->GetValue() &&
         m_fallbackCurrentSource &&
         m_fallbackCurrentSource->GetStringSelection() ==
             _("Offline current (.xtd)");
}

bool EnvironmentalGribDialog::ValidateOfflineTidalPackage() {
  m_offlineTidalPackageValid = false;
  m_offlineClimatologyAvailable = false;
  const wxString path = m_offlineTidalFile->GetPath();
  if (path.empty()) {
    m_offlineTidalStatus->SetValue(_("File not configured"));
    return false;
  }
  if (!wxFileName::FileExists(path)) {
    m_offlineTidalStatus->SetValue(
        wxString::Format(_("File not found: %s"), path));
    return false;
  }

  wxArrayString standardOutput;
  wxArrayString standardError;
  const wxString command =
      xgrib::QuoteProcessArgument(m_generatorPath->GetValue()) +
      " inspect-xtd " + xgrib::QuoteProcessArgument(path);
  const long exitCode =
      wxExecute(command, standardOutput, standardError, wxEXEC_SYNC);
  const wxString output = JoinLines(standardOutput);
  wxString detail = JoinLines(standardError);
  if (detail.empty()) detail = output;
  detail = ConciseText(Redact(detail));

  if (exitCode != 0) {
    if (detail.empty()) {
      detail =
          exitCode == -1
              ? _("The native helper could not be launched.")
              : wxString::Format(_("The native helper exited with status %ld."),
                                 exitCode);
    }
    m_offlineTidalStatus->SetValue(OfflineTidalFailureStatus(detail) + ": " +
                                   detail);
    return false;
  }

  wxJSONValue metadata;
  wxJSONReader reader;
  int parseErrors = reader.Parse(output, &metadata);
  if (parseErrors != 0 || !metadata.IsObject()) {
    const size_t firstBrace = output.find('{');
    const size_t lastBrace = output.rfind('}');
    if (firstBrace != wxString::npos && lastBrace != wxString::npos &&
        lastBrace > firstBrace) {
      parseErrors = reader.Parse(
          output.Mid(firstBrace, lastBrace - firstBrace + 1), &metadata);
    }
  }
  if (parseErrors != 0 || !metadata.IsObject()) {
    m_offlineTidalStatus->SetValue(
        _("Inspection failed: helper returned invalid metadata"));
    return false;
  }
  if ((metadata.HasMember("valid") && metadata.ItemAt("valid").IsBool() &&
       !metadata.ItemAt("valid").AsBool()) ||
      (metadata.HasMember("authenticated") &&
       metadata.ItemAt("authenticated").IsBool() &&
       !metadata.ItemAt("authenticated").AsBool())) {
    m_offlineTidalStatus->SetValue(
        _("Authentication/integrity failure: package validation failed"));
    return false;
  }
  if (metadata.HasMember("capabilities") &&
      metadata.ItemAt("capabilities").IsObject()) {
    const wxJSONValue capabilities = metadata.ItemAt("capabilities");
    if (capabilities.HasMember("tide_expected_seasonal") &&
        capabilities.ItemAt("tide_expected_seasonal").IsBool()) {
      m_offlineClimatologyAvailable =
          capabilities.ItemAt("tide_expected_seasonal").AsBool();
    }
  }

  double coverageWest = 0.0;
  double coverageSouth = 0.0;
  double coverageEast = 0.0;
  double coverageNorth = 0.0;
  const bool haveCoverage = JsonBoundingBox(
      metadata, &coverageWest, &coverageSouth, &coverageEast, &coverageNorth);
  double requestedWest = 0.0;
  double requestedSouth = 0.0;
  double requestedEast = 0.0;
  double requestedNorth = 0.0;
  const bool haveRequestedArea =
      m_west->GetValue().ToDouble(&requestedWest) &&
      m_south->GetValue().ToDouble(&requestedSouth) &&
      m_east->GetValue().ToDouble(&requestedEast) &&
      m_north->GetValue().ToDouble(&requestedNorth);
  if (haveCoverage && haveRequestedArea &&
      !xgrib::AreaContains({"", "", coverageWest, coverageSouth, coverageEast, coverageNorth},
                          {"", "", requestedWest, requestedSouth, requestedEast, requestedNorth})) {
    m_offlineTidalStatus->SetValue(wxString::Format(
        _("Requested area outside coverage: package covers %.4f, %.4f to %.4f, "
          "%.4f"),
        coverageWest, coverageSouth, coverageEast, coverageNorth));
    return false;
  }

  wxString model =
      FirstJsonText(metadata, {"model_name", "model_source", "source"});
  const wxString description = FirstJsonText(
      metadata,
      {"model_source_description", "source_description", "description"});
  if (!description.empty() && description != model) {
    if (!model.empty()) model += " - ";
    model += description;
  }
  if (model.empty()) model = _("Not provided");

  wxString coverageText = FirstJsonText(metadata, {"coverage_description"});
  if (coverageText.empty() && haveCoverage) {
    coverageText = wxString::Format(_("%.4f, %.4f to %.4f, %.4f"), coverageWest,
                                    coverageSouth, coverageEast, coverageNorth);
  }
  if (coverageText.empty() && metadata.HasMember("coverage") &&
      metadata.ItemAt("coverage").IsString()) {
    coverageText = metadata.ItemAt("coverage").AsString();
  }
  if (coverageText.empty()) coverageText = _("Not provided");

  wxString resolution = FirstJsonText(
      metadata, {"nominal_resolution", "resolution", "grid_resolution",
                 "grid_spacing_degrees", "grid_spacing_deg"});
  if (resolution.empty()) resolution = _("Not provided");
  wxString formatVersion = FirstJsonText(metadata, {"format_version"});
  if (formatVersion.empty()) formatVersion = _("Not provided");

  wxString constituentCount;
  if (metadata.HasMember("constituents") &&
      metadata.ItemAt("constituents").IsArray()) {
    constituentCount =
        wxString::Format("%d", metadata.ItemAt("constituents").Size());
  } else {
    constituentCount = FirstJsonText(metadata, {"constituent_count"});
  }
  if (constituentCount.empty()) constituentCount = _("Not provided");

  wxString packageVersion = FirstJsonText(metadata, {"package_version"});
  wxString buildId = FirstJsonText(
      metadata, {"build_id", "package_build_id", "package_build"});
  if (metadata.HasMember("package") && metadata.ItemAt("package").IsObject()) {
    const wxJSONValue package = metadata.ItemAt("package");
    if (packageVersion.empty()) {
      packageVersion = FirstJsonText(package, {"version", "package_version"});
    }
    if (buildId.empty()) {
      buildId = FirstJsonText(package, {"build_id", "build"});
    }
  }
  wxString packageBuild = packageVersion;
  if (!buildId.empty()) {
    if (!packageBuild.empty()) packageBuild += " / ";
    packageBuild += buildId;
  }
  if (packageBuild.empty()) packageBuild = _("Not provided");

  wxString status = _("Ready");
  status += "\n" + wxString::Format(_("Model/source: %s"), model);
  status += "\n" + wxString::Format(_("Coverage: %s"), coverageText);
  status += "\n" + wxString::Format(_("Nominal resolution: %s"), resolution);
  status += "\n" + wxString::Format(_("Format version: %s"), formatVersion);
  status +=
      "\n" + wxString::Format(_("Constituent count: %s"), constituentCount);
  status +=
      "\n" + wxString::Format(_("Package version/build ID: %s"), packageBuild);
  status += "\n" + wxString::Format(_("Expected seasonal circulation: %s"),
                                    m_offlineClimatologyAvailable
                                        ? _("Available")
                                        : _("Not available"));
  m_offlineTidalStatus->SetValue(status);
  m_offlineCurrentMode->Enable(m_offlineClimatologyAvailable);
  if (!m_offlineClimatologyAvailable) m_offlineCurrentMode->SetSelection(0);
  m_offlineTidalPackageValid = true;
  return true;
}

void EnvironmentalGribDialog::UpdateProviderUi() {
  QueueEstimate();
  int mode = m_mode->GetSelection();
  wxString provider = m_provider->GetStringSelection();
  bool forecastMode = mode == 0;
  bool tpxoMode = mode == 1;
  bool netcdfMode = mode == 2;
  bool syntheticMode = mode == 3;
  bool weatherEnabled = m_generateWeather->GetValue() &&
                        m_weatherProvider->GetStringSelection() != "None";
  bool currentsEnabled = m_generateCurrents->GetValue() &&
                         m_currentSource->GetStringSelection() != "None";
  bool weatherExisting =
      weatherEnabled &&
      m_weatherProvider->GetStringSelection().Contains("Existing");
  bool currentExisting =
      currentsEnabled &&
      m_currentSource->GetStringSelection().Contains("Existing");
  bool currentTpxoCache =
      currentsEnabled &&
      m_currentSource->GetStringSelection().Contains("TPXO cache");
  bool currentTpxoDirect =
      currentsEnabled &&
      m_currentSource->GetStringSelection().Contains("TPXO direct");
  bool currentMarine =
      currentsEnabled &&
      m_currentSource->GetStringSelection().Contains("Marine.ie");
  bool currentRtofs = currentsEnabled &&
                      m_currentSource->GetStringSelection().Contains("RTOFS");
  bool currentOfs =
      currentsEnabled && m_currentSource->GetStringSelection().Contains("OFS");
  bool currentIbi =
      currentsEnabled && m_currentSource->GetStringSelection().Contains("IBI");
  bool currentMediterranean =
      currentsEnabled &&
      m_currentSource->GetStringSelection().Contains("Mediterranean");
  bool currentOfflineTidal = currentsEnabled && IsOfflineTidalSelected();
  bool currentOfflineNeeded = currentsEnabled && IsOfflineTidalNeeded();
  bool weatherGfs =
      weatherEnabled && m_weatherProvider->GetStringSelection().Contains("GFS");
  bool weatherHrrr = weatherEnabled &&
                     m_weatherProvider->GetStringSelection().Contains("HRRR");
  bool weatherUkv =
      weatherEnabled &&
      m_weatherProvider->GetStringSelection().Contains("Met Office UKV");
  bool weatherMetNo =
      weatherEnabled &&
      m_weatherProvider->GetStringSelection().Contains("MET Norway");
  bool weatherIconEu =
      weatherEnabled &&
      m_weatherProvider->GetStringSelection().Contains("ICON-EU");
  bool weatherAifs = weatherEnabled &&
                     m_weatherProvider->GetStringSelection().Contains("AIFS");
  bool weatherGenerated = weatherEnabled && !weatherExisting;
  bool waveEnabled = m_includeWaves->GetValue();
  bool waveCopernicus =
      waveEnabled &&
      m_waveProvider->GetStringSelection().Contains("Copernicus");
  bool needsCopernicusCredentials = NeedsCopernicusCredentials();
  bool showTpxoModel = currentTpxoDirect || currentTpxoCache;
  bool extensionEnabled = m_extendForecast->GetValue();

  auto showPair = [](wxWindow* label, wxWindow* control, bool show) {
    if (label) label->Show(show);
    if (control) control->Show(show);
  };

  m_provider->Enable(false);
  showPair(m_fallbackWeatherLabel, m_fallbackWeatherProvider,
           extensionEnabled && weatherEnabled);
  showPair(m_fallbackWaveLabel, m_fallbackWaveProvider,
           extensionEnabled && waveEnabled);
  showPair(m_fallbackCurrentLabel, m_fallbackCurrentSource,
           extensionEnabled && currentsEnabled);
  m_fallbackWeatherProvider->Enable(extensionEnabled && weatherEnabled &&
                                    !weatherGfs && !m_processRunning);
  m_fallbackWaveProvider->Enable(
      extensionEnabled && waveEnabled &&
      !m_waveProvider->GetStringSelection().Contains("NOAA GFS") &&
      !m_processRunning);
  m_fallbackCurrentSource->Enable(extensionEnabled && currentsEnabled &&
                                  !currentOfflineTidal && !m_processRunning);
  m_weatherProvider->Enable(m_generateWeather->GetValue());
  showPair(m_weatherPresetLabel, m_weatherPreset, weatherGenerated);
  showPair(m_wavesLabel, m_includeWaves, true);
  showPair(m_waveProviderLabel, m_waveProvider, waveEnabled);
  showPair(m_existingWeatherFileLabel, m_existingWeatherFile, weatherExisting);
  showPair(m_existingWeatherPathLabel, m_existingWeatherPath, weatherExisting);
  m_weatherPreset->Enable(weatherGenerated);
  m_includeWaves->Enable(true);
  m_waveProvider->Enable(waveEnabled);
  m_existingWeatherFile->Enable(weatherExisting);
  m_currentSource->Enable(m_generateCurrents->GetValue());
  showPair(m_existingCurrentFileLabel, m_existingCurrentFile, currentExisting);
  showPair(m_existingCurrentPathLabel, m_existingCurrentPath, currentExisting);
  showPair(m_offlineTidalFileLabel, m_offlineTidalFile, currentOfflineNeeded);
  showPair(m_offlineTidalStatusLabel, m_offlineTidalStatus,
           currentOfflineNeeded);
  showPair(m_offlineCurrentModeLabel, m_offlineCurrentMode,
           currentOfflineNeeded);
  showPair(m_usernameLabel, m_username, needsCopernicusCredentials);
  showPair(m_passwordLabel, m_password, needsCopernicusCredentials);
  showPair(m_tpxoModelDirLabel, m_tpxoModelDir, showTpxoModel);
  showPair(m_tpxoModelNameLabel, m_tpxoModelName, showTpxoModel);
  showPair(m_tpxoGridSpacingLabel, m_tpxoGridSpacing, showTpxoModel);
  showPair(m_checkTpxoLabel, m_checkTpxoButton, showTpxoModel);
  showPair(m_tpxoCacheFileLabel, m_tpxoCacheFile, currentTpxoCache);
  showPair(m_prepareTpxoCacheLabel, m_prepareTpxoCacheButton, currentTpxoCache);
  showPair(m_localNetcdfLabel, m_localNetcdf, false);
  m_rememberUsername->Show(needsCopernicusCredentials);
  m_existingCurrentFile->Enable(currentExisting);
  m_offlineTidalFile->Enable(currentOfflineNeeded && !m_processRunning);
  m_offlineCurrentMode->Enable(currentOfflineNeeded &&
                               m_offlineClimatologyAvailable &&
                               !m_processRunning);
  m_username->Enable(needsCopernicusCredentials);
  m_password->Enable(needsCopernicusCredentials);
  m_rememberUsername->Enable(needsCopernicusCredentials);
  m_tpxoModelDir->Enable(showTpxoModel);
  m_tpxoModelName->Enable(showTpxoModel);
  m_tpxoGridSpacing->Enable(showTpxoModel);
  m_checkTpxoButton->Enable(showTpxoModel && !m_processRunning);
  m_useTpxoCache->Enable(false);
  m_tpxoCacheFile->Enable(currentTpxoCache);
  m_prepareTpxoCacheButton->Enable(currentTpxoCache && !m_processRunning);
  m_localNetcdf->Enable(false);

  wxString currentNote;
  if (currentOfflineTidal) {
    currentNote =
        m_offlineCurrentMode->GetSelection() == 1
            ? _("Current source: authenticated offline XTD package. Output "
                "combines deterministic astronomical tide with expected "
                "seasonal circulation. The climatological component is not a "
                "forecast and does not predict eddies, surge, storms, or "
                "short-term anomalies.")
            : _("Current source: authenticated offline XTD package. "
                "Astronomical tidal currents are generated locally from the "
                "selected multi-source package; no network access or original "
                "source dataset is required.");
  } else if (currentRtofs) {
    currentNote =
        "Current source: NOAA RTOFS Global ocean-current forecast. Global/no "
        "account. "
        "Useful for offshore ocean-current routing, including Gulf Stream-type "
        "circulation where model guidance is available. "
        "Current guidance is typically 6-hourly.";
  } else if (currentOfs) {
    currentNote =
        "Current source: NOAA OFS/S-111 coastal current forecast. Experimental "
        "stub. "
        "U.S. coastal waters and Great Lakes. Not yet a complete GRIB "
        "generator.";
  } else if (currentIbi) {
    currentNote =
        "Current source: Copernicus Marine IBI 1/36-degree hourly surface "
        "current analysis/forecast. Includes tidal and non-tidal model "
        "processes. Coverage is 26.17-56.08 N, 19.08 W-5.08 E; account "
        "required.";
  } else if (currentMediterranean) {
    currentNote =
        "Current source: Copernicus Marine Mediterranean 4.2 km hourly "
        "surface current analysis/forecast. Includes tidal and non-tidal "
        "model processes. Coverage is 30.19-45.98 N, 17.29 W-36.29 E; "
        "account required.";
  }

  if (!weatherEnabled && waveEnabled) {
    wxString note =
        waveCopernicus
            ? "Wave source: Copernicus Marine Global Waves forecast. Account "
              "required; global 3-hourly wave fields."
            : "Wave source: NOAA GFS Wave. No account required; includes "
              "significant wave height, primary wave period, and primary wave "
              "direction.";
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherMetNo) {
    wxString note =
        "Source: MET Norway Nordic 1 km postprocessed forecast, derived from "
        "the operational Nordic model. No account is required. The helper "
        "reads only the requested NetCDF subset and converts its Lambert grid "
        "to OpenCPN GRIB2. Forecast data is hourly for about 56 hours; select "
        "forecast extension with GFS for longer requests.";
    if (waveCopernicus) {
      note +=
          "\nWave source: Copernicus Marine Global Waves forecast. Account "
          "required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherUkv) {
    wxString note =
        "Source: Met Office UKV 2 km forecast. Met Office UKV 2 km is a "
        "high-resolution UK/Ireland short-range forecast. "
        "The plugin converts the source NetCDF data to OpenCPN GRIB in the "
        "background.\n"
        "UKV weather is hourly to about 54h and 3-hourly thereafter. Currents "
        "may remain hourly. "
        "Requests outside the UK/Ireland domain or beyond available hours will "
        "fail clearly.";
    if (waveCopernicus) {
      note +=
          "\nWave source: Copernicus Marine Global Waves forecast. Account "
          "required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherHrrr) {
    wxString note =
        "Source: NOAA HRRR 3 km forecast. Short-range hourly NOAA HRRR for the "
        "contiguous United States. "
        "No account. Currently uses full-grid GRIB messages; files may be "
        "large because bbox cropping is not yet implemented.";
    if (waveCopernicus) {
      note +=
          "\nWave source: Copernicus Marine Global Waves forecast. Account "
          "required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherIconEu) {
    wxString note =
        "Source: DWD ICON-EU regional forecast. DWD ICON-EU regional forecast "
        "for Europe. "
        "No account. Currently uses full-domain DWD field files; files may be "
        "large because bbox cropping is not yet implemented.";
    if (waveCopernicus) {
      note +=
          "\nWave source: Copernicus Marine Global Waves forecast. Account "
          "required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherAifs) {
    wxString note =
        "Source: ECMWF AIFS Open Data forecast. Global AI forecast, no "
        "account. "
        "Experimental provider using the operational AIFS Single v2 feed. "
        "Files may be large if not cropped. "
        "Forecast steps are 6-hourly or coarser.";
    if (waveCopernicus) {
      note +=
          "\nWave source: Copernicus Marine Global Waves forecast. Account "
          "required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherEnabled &&
             m_weatherProvider->GetStringSelection().Contains("ECMWF")) {
    wxString note =
        "Source: ECMWF IFS Open Data forecast. ECMWF IFS Open Data is "
        "global/medium-range and currently not "
        "spatially cropped; files may be large. Forecast steps are 3-hourly or "
        "coarser.";
    if (waveCopernicus) {
      note +=
          "\nWave source: Copernicus Marine Global Waves forecast. Account "
          "required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherGfs) {
    wxString note =
        "Source: NOAA GFS forecast via NOMADS. Bbox-subset weather is compact; "
        "optional wave fields add significant wave height, primary wave "
        "period, and primary wave direction.";
    if (waveCopernicus) {
      note +=
          "\nWave source: Copernicus Marine Global Waves forecast. Account "
          "required; global 3-hourly wave fields.";
    }
    if (m_includeWaves->GetValue() && m_stepHours->GetValue() != 3) {
      note += wxString::Format(
          "\nWave fields are included every 3 hours; wind/weather and currents "
          "remain every %d hour%s.",
          m_stepHours->GetValue(), m_stepHours->GetValue() == 1 ? "" : "s");
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (currentTpxoCache) {
    m_providerNote->SetLabel(
        "Source: TPXO10 astronomical tide model cache. TPXO provides "
        "astronomical tidal currents from local licensed model/cache data. It "
        "does not model Gulf Stream, surge, wind residuals, or river flow.");
  } else if (currentTpxoDirect) {
    m_providerNote->SetLabel(
        "Source: TPXO10 astronomical tide model. TPXO provides astronomical "
        "tidal currents from local licensed model/cache data. It does not "
        "model Gulf Stream, surge, wind residuals, or river flow.");
  } else if (currentMarine) {
    m_providerNote->SetLabel(
        "Source: Marine.ie Irish Sea model current GRIB. No Copernicus "
        "credentials required. Valid time range depends on provider run time.");
  } else if (!currentNote.empty()) {
    m_providerNote->SetLabel(currentNote);
  } else if (needsCopernicusCredentials) {
    m_providerNote->SetLabel(
        "Source: Copernicus Marine model ocean currents. Account required. "
        "Username/password are used for this operation only; password is "
        "passed via environment, not command line.");
  } else if (!currentsEnabled) {
    m_providerNote->SetLabel(
        "Currents disabled. Output will be weather-only if weather is "
        "enabled.");
  } else {
    m_providerNote->SetLabel("");
  }
  m_scrolled->Layout();
  m_scrolled->FitInside();
  m_scrolled->SetVirtualSize(m_scrolled->GetSizer()->GetMinSize());
  Layout();
}

bool EnvironmentalGribDialog::ConfirmLargeCopernicusRequest() {
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  bool parsed = m_west->GetValue().ToDouble(&west) &&
                m_south->GetValue().ToDouble(&south) &&
                m_east->GetValue().ToDouble(&east) &&
                m_north->GetValue().ToDouble(&north);
  double area = parsed ? (east > west ? east - west : east + 360.0 - west) * (north - south) : 0.0;
  if (m_durationHours->GetValue() <= 72 && area <= 12.0) {
    return true;
  }
  wxString message =
      "This Copernicus request is larger than the normal v1 default.\n\n"
      "Duration: " +
      wxString::Format("%d hours", m_durationHours->GetValue()) +
      "\nApproximate bbox area: " +
      wxString::Format("%.2f square degrees", area) + "\n\nContinue?";
  return wxMessageBox(message, "Confirm Copernicus download",
                      wxYES_NO | wxICON_WARNING, this) == wxYES;
}

bool EnvironmentalGribDialog::ValidateUkvRequest() {
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  if (m_west->GetValue().ToDouble(&west) &&
      m_south->GetValue().ToDouble(&south) &&
      m_east->GetValue().ToDouble(&east) &&
      m_north->GetValue().ToDouble(&north)) {
    if (west > east || west < -12.0 || east > 4.0 || south < 48.0 || north > 62.0) {
      wxString message =
          "The requested bbox is outside the Met Office UKV UK/Ireland "
          "regional domain. Choose a UK/Ireland area or use GFS/ECMWF.";
      AppendLog(message);
      wxMessageBox(message, "UKV area unavailable", wxOK | wxICON_WARNING,
                   this);
      return false;
    }
  }
  return true;
}

bool EnvironmentalGribDialog::ValidateMetNoRequest() {
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  if (m_west->GetValue().ToDouble(&west) &&
      m_south->GetValue().ToDouble(&south) &&
      m_east->GetValue().ToDouble(&east) &&
      m_north->GetValue().ToDouble(&north) &&
      (west > east || west < -20.0 || east > 80.0 || south < 51.0 || north > 88.0)) {
    const wxString message =
        "The requested bbox is outside the MET Norway Nordic forecast domain. "
        "Choose a Nordic/European area or use a global provider.";
    AppendLog(message);
    wxMessageBox(message, "MET Norway area unavailable",
                 wxOK | wxICON_WARNING, this);
    return false;
  }
  return true;
}

void EnvironmentalGribDialog::OnCancel(wxCommandEvent&) {
  if (!m_processRunning || m_processPid == 0) {
    AppendLog("No running process to cancel.");
    return;
  }
  if (!ChildProcessStillExists()) {
    AppendLog(wxString::Format(
        "Process pid=%ld has already exited; cleaning up dialog state.",
        m_processPid));
    DrainProcessOutput();
    FlushProcessOutput();
    FinishCommand(-1, true);
    return;
  }
  m_processCancelled = true;
  AppendLog(wxString::Format("Cancelling process, pid=%ld", m_processPid));
  wxKillError error = wxKILL_OK;
  xgrib::TerminateGeneratorProcess(m_processPid, &error);
  if (error != wxKILL_OK) {
    AppendLog(wxString::Format("Process cancel request returned wxKillError=%d",
                               static_cast<int>(error)));
    if (!ChildProcessStillExists()) {
      AppendLog(
          "Process was already gone after cancel request; treating it as "
          "exited.");
      DrainProcessOutput();
      FlushProcessOutput();
      FinishCommand(-1, true);
      return;
    }
  }
}

void EnvironmentalGribDialog::OnClose(wxCommandEvent& event) {
  if (m_processRunning) {
    int response = wxMessageBox(
        "A generation or dependency check is still running. Cancel it?",
        "Operation running", wxYES_NO | wxICON_QUESTION, this);
    if (response == wxYES) {
      OnCancel(event);
    }
    return;
  }
  (void)event;
  SaveSettings();
  Hide();
}

void EnvironmentalGribDialog::OnDialogClose(wxCloseEvent& event) {
  if (m_processRunning) {
    int response = wxMessageBox(
        "A generation or dependency check is still running. Cancel it?",
        "Operation running", wxYES_NO | wxICON_QUESTION, this);
    if (response == wxYES) {
      wxCommandEvent dummy;
      OnCancel(dummy);
    }
    event.Veto();
    return;
  }
  SaveSettings();
  Hide();
}

void EnvironmentalGribDialog::OnProcessTimer(wxTimerEvent& event) {
  (void)event;
  DrainProcessOutput();
  if (m_processRunning) {
    const long seconds = ((wxGetUTCTimeMillis() - m_processStarted) / 1000).ToLong();
    const wxString status = wxString::Format(
        m_processCancelled ? _("Cancelling helper (pid %ld), %ld seconds elapsed")
                           : _("Running helper (pid %ld), %ld seconds elapsed"),
        m_processPid, seconds);
    if (m_processStatus->GetLabel() != status) m_processStatus->SetLabel(status);
  }
  if (m_processRunning && m_processPid != 0 && !ChildProcessStillExists()) {
    AppendLog(
        wxString::Format("Process pid=%ld is no longer running; finalizing "
                         "without wxEVT_END_PROCESS.",
                         m_processPid));
    FlushProcessOutput();
    FinishCommand(-1, true);
  }
}

void EnvironmentalGribDialog::ShowEstimate(const wxString& file,
                                           const wxString& decoded) {
  m_estimateFile = file;
  m_estimateDecoded = decoded;
  m_estimateSummary->SetLabel(file + "\n" + decoded + "\n" +
      _("Generation and routing need extra RAM; this is not a RAM limit."));
  m_estimateSummary->Wrap(std::max(200, m_scrolled->GetClientSize().x - 32));
  const int lines = 1 + m_estimateSummary->GetLabel().Freq('\n');
  m_estimateSummary->SetMinSize(wxSize(100, GetCharHeight() * lines + 6));
  if (m_scrolled->GetSizer()) {
    m_scrolled->GetSizer()->Layout();
    m_scrolled->FitInside();
  }
}

void EnvironmentalGribDialog::QueueEstimate() {
  if (!m_estimateSummary) return;
  wxString request, error;
  const bool valid = BuildGenerateJobText(&request, &error);
  const wxString key = m_generatorPath->GetValue() + "\n" +
                       (valid ? request : "invalid: " + error);
  if (key == m_lastEstimateRequest) return;
  m_lastEstimateRequest = key;
  ++m_estimateRevision;
  if (!valid) {
    StopEstimate();
    ShowEstimate(_("Estimated GRIB file: check settings"),
                 _("Decoded forecast data: not yet known"));
    m_estimateSummary->SetToolTip(error);
    return;
  }
  ShowEstimate(_("Estimated GRIB file: updating..."),
               _("Decoded forecast data: updating..."));
  // Never display a previous request's estimate while an edit is pending.
  m_estimateSummary->UnsetToolTip();
  if (!m_estimateProcess) m_estimateTimer.StartOnce(400);
}

void EnvironmentalGribDialog::StopEstimate() {
  m_estimateTimer.Stop();
  if (m_estimateProcess) {
    if (m_estimatePid) wxKill(m_estimatePid, wxSIGKILL, nullptr, wxKILL_CHILDREN);
    m_estimateProcess->Detach();
    m_estimateProcess = nullptr;
  }
  m_estimatePid = 0;
  if (!m_estimateJobPath.empty()) {
    wxRemoveFile(m_estimateJobPath);
    m_estimateJobPath.clear();
  }
}

void EnvironmentalGribDialog::DrainEstimateOutput() {
  if (!m_estimateProcess) return;
  // Bounded response and work per UI tick, even with a misconfigured helper.
  for (auto* stream : {m_estimateProcess->GetInputStream(),
                       m_estimateProcess->GetErrorStream()}) {
    for (int chunk = 0; stream && stream->CanRead() && chunk < 16; ++chunk) {
      char bytes[4096];
      stream->Read(bytes, sizeof(bytes));
      if (!stream->LastRead()) break;
      if (stream == m_estimateProcess->GetInputStream() &&
          m_estimateOutput.length() < 65536)
        m_estimateOutput += wxString::FromUTF8(bytes, stream->LastRead());
    }
  }
}

void EnvironmentalGribDialog::OnEstimateTimer(wxTimerEvent&) {
  if (m_estimateProcess) {
    DrainEstimateOutput();
    if (wxGetUTCTimeMillis() - m_estimateStarted > 10000) {
      StopEstimate();
      ShowEstimate(_("Estimated GRIB file: unavailable (helper timed out)"),
                   _("Decoded forecast data: not yet known"));
      if (m_runningEstimateRevision != m_estimateRevision)
        m_estimateTimer.StartOnce(400);
    }
    return;
  }
  m_runningEstimateRevision = m_estimateRevision;
  m_estimateJobPath = wxFileName::CreateTempFileName("xgrib-estimate-");
  wxString error;
  if (m_estimateJobPath.empty() || !WriteGenerateJob(m_estimateJobPath, &error)) {
    StopEstimate();
    ShowEstimate(_("Estimated GRIB file: check settings"),
                 _("Decoded forecast data: not yet known"));
    m_estimateSummary->SetToolTip(error);
    return;
  }
  m_estimateOutput.clear();
  m_estimateProcess = new wxProcess(this);
  m_estimateProcess->Redirect();
  const wxString command =
      xgrib::QuoteProcessArgument(m_generatorPath->GetValue()) +
      " estimate-job --job " + xgrib::QuoteProcessArgument(m_estimateJobPath);
  // Separate process from generation: estimate-job only parses this small job
  // and does arithmetic. No download, model loading or credential handling.
  m_estimatePid = wxExecute(command, wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE |
                                       wxEXEC_MAKE_GROUP_LEADER,
                            m_estimateProcess);
  if (m_estimatePid <= 0) {
    delete m_estimateProcess;
    m_estimateProcess = nullptr;
    StopEstimate();
    ShowEstimate(_("Estimated GRIB file: helper unavailable"),
                 _("Decoded forecast data: not yet known"));
    return;
  }
  m_estimateStarted = wxGetUTCTimeMillis();
  m_estimateTimer.Start(100);
}

void EnvironmentalGribDialog::FinishEstimate(long exit_code) {
  m_estimateTimer.Stop();
  delete m_estimateProcess;
  m_estimateProcess = nullptr;
  m_estimatePid = 0;
  wxRemoveFile(m_estimateJobPath);
  m_estimateJobPath.clear();
  if (m_runningEstimateRevision != m_estimateRevision) {
    m_estimateTimer.StartOnce(400);
    return;
  }
  wxJSONValue estimate;
  wxJSONReader reader;
  if (exit_code != 0 || reader.Parse(m_estimateOutput, &estimate) != 0 ||
      !estimate.HasMember("schemaVersion") || !estimate["schemaVersion"].IsInt() ||
      !estimate.HasMember("knownDecodedBytes") || !estimate["components"].IsArray() ||
      estimate["schemaVersion"].AsInt() != 1) {
    ShowEstimate(_("Estimated GRIB file: unavailable; check settings/helper"),
                 _("Decoded forecast data: not yet known"));
    return;
  }
  m_timeSettingsIssue.clear();
  m_timeSettingsRevision = m_runningEstimateRevision;
  if (estimate["preflight"]["issues"].Size() > 0) {
    wxJSONWriter writer;
    writer.Write(estimate["preflight"]["issues"].ItemAt(0), m_timeSettingsIssue);
  }
  auto mib = [&](const wxString& key) {
    double value = 0;
    estimate[key].AsString().ToDouble(&value);
    return value / 1048576.0;
  };
  const wxString file = estimate.HasMember("fileUpperBytes")
      ? wxString::Format(_("Estimated GRIB file: up to %.2f MiB (packing varies)"),
                         mib("fileUpperBytes"))
      : _("Estimated GRIB file: not yet known");
  const wxString decoded = estimate.HasMember("decodedBytes")
      ? wxString::Format(_("Decoded numeric data: approximately %.2f MiB"),
                         mib("decodedBytes"))
      : wxString::Format(_("Decoded numeric data: %.2f MiB known; total not yet known"),
                         mib("knownDecodedBytes"));
  ShowEstimate(file, decoded);
  wxString detail;
  for (int i = 0; i < estimate["components"].Size(); ++i) {
    auto item = estimate["components"].ItemAt(i);
    detail += item["component"].AsString() + ": " + item["status"].AsString();
    if (item.HasMember("reason")) detail += L" — " + item["reason"].AsString();
    detail += "\n";
  }
  m_estimateSummary->SetToolTip(detail);
}

void EnvironmentalGribDialog::OnProcessTerminated(wxProcessEvent& event) {
  if (m_estimateProcess && event.GetPid() == m_estimatePid) {
    DrainEstimateOutput();
    FinishEstimate(event.GetExitCode());
    return;
  }
  if (!m_processRunning || event.GetPid() != m_processPid) {
    AppendLog(
        wxString::Format("Ignoring stale process completion event, pid=%d",
                         static_cast<int>(event.GetPid())));
    return;
  }
  AppendLog(wxString::Format("Process completed, pid=%d",
                             static_cast<int>(event.GetPid())));
  DrainProcessOutput();
  FlushProcessOutput();
  FinishCommand(event.GetExitCode(), true, true);
}

void EnvironmentalGribDialog::AppendLog(const wxString& message) {
  m_log->AppendText(message + "\n");
}

void EnvironmentalGribDialog::DrainStream(wxInputStream* stream,
                                          wxString* buffer,
                                          const wxString& prefix) {
  if (!stream || !buffer) return;
  while (stream->CanRead()) {
    char ch = static_cast<char>(stream->GetC());
    if (stream->LastRead() == 0) break;
    if (ch == '\r') continue;
    if (ch == '\n') {
      AppendLog(Redact(prefix + *buffer));
      buffer->clear();
    } else {
      *buffer += wxString::FromUTF8(&ch, 1);
    }
  }
}

void EnvironmentalGribDialog::DrainProcessOutput() {
  if (!m_process) return;
  DrainStream(m_process->GetInputStream(), &m_stdoutBuffer, "");
  DrainStream(m_process->GetErrorStream(), &m_stderrBuffer, "stderr: ");
}

void EnvironmentalGribDialog::FlushProcessOutput() {
  if (!m_stdoutBuffer.empty()) {
    AppendLog(Redact(m_stdoutBuffer));
    m_stdoutBuffer.clear();
  }
  if (!m_stderrBuffer.empty()) {
    AppendLog(Redact("stderr: " + m_stderrBuffer));
    m_stderrBuffer.clear();
  }
}

void EnvironmentalGribDialog::StartCommand(const wxString& command,
                                           const wxString& password,
                                           bool generation) {
  AppendLog("StartCommand begins");
  if (m_processRunning) {
    AppendLog("Another operation is already running.");
    return;
  }
  m_currentCommand = command;
  m_processGeneration = generation;
  m_processCancelled = false;
  m_processPid = 0;
  m_stdoutBuffer.clear();
  m_stderrBuffer.clear();
  SetBusy(true);
  AppendLog("Command: " + Redact(command));
  if (!password.empty()) {
    AppendLog(
        "Copernicus password will be passed to the native helper through an "
        "environment variable, not on the command line or in the job file.");
  }

  auto* process = new wxProcess(this);
  process->Redirect();

  wxExecuteEnv env;
  // wxExecuteEnv replaces rather than augments the child environment when its
  // map is non-empty. Preserve the parent environment before adding the
  // secret or packaged runtime paths.
  wxGetEnvMap(&env.env);
  if (!password.empty()) {
    env.env["ENVIRONMENTAL_GRIB_COPERNICUS_PASSWORD"] = password;
  }

  wxFileName generator(m_generatorPath->GetValue());
  wxFileName pluginRoot(generator.GetPath(), "");
  pluginRoot.RemoveLastDir();
  wxFileName runtime(pluginRoot.GetPath(), "");
  runtime.AppendDir("runtime");
  wxFileName definitions(runtime.GetPath(), "");
  definitions.AppendDir("share");
  definitions.AppendDir("eccodes");
  definitions.AppendDir("definitions");
  wxFileName samples(runtime.GetPath(), "");
  samples.AppendDir("share");
  samples.AppendDir("eccodes");
  samples.AppendDir("samples");
  wxFileName proj(runtime.GetPath(), "");
  proj.AppendDir("share");
  proj.AppendDir("proj");
  if (definitions.DirExists())
    env.env["ECCODES_DEFINITION_PATH"] = definitions.GetPath();
  if (samples.DirExists()) env.env["ECCODES_SAMPLES_PATH"] = samples.GetPath();
  if (proj.DirExists()) env.env["PROJ_DATA"] = proj.GetPath();

  long pid = wxExecute(command, wxEXEC_ASYNC | wxEXEC_NODISABLE |
                                   wxEXEC_HIDE_CONSOLE | wxEXEC_MAKE_GROUP_LEADER,
                       process, &env);
  if (pid == 0) {
    AppendLog("Process failed to launch");
    wxLogError("xGRIB environmental generator failed to launch");
    delete process;
    FinishCommand(-1, false);
    return;
  }

  m_process = process;
  m_processRunning = true;
  m_processPid = pid;
  m_processStarted = wxGetUTCTimeMillis();
  m_processStatus->SetLabel(wxString::Format(_("Running helper (pid %ld)"), pid));
  AppendLog(wxString::Format("Process launched, pid=%ld", pid));
  wxLogMessage("xGRIB environmental generator launched, pid=%ld", pid);
  if (!m_processTimer.Start(100)) {
    AppendLog(
        "Failed to start the process-output monitor; terminating the helper to "
        "avoid an output-pipe deadlock.");
    wxKillError error = wxKILL_OK;
    xgrib::TerminateGeneratorProcess(pid, &error);
    AppendLog(wxString::Format("Helper termination requested (wxKillError=%d).",
                               static_cast<int>(error)));
  }
}

void EnvironmentalGribDialog::FinishCommand(long exit_code, bool launched,
                                            bool completion_notified) {
  m_processTimer.Stop();
  if (m_process) {
    // wxExecute still holds this pointer until it delivers OnTerminate.
    // Polling can detect exit first; detach and let that notification delete
    // the object instead of leaving wxExecute with a dangling pointer.
    if (completion_notified)
      delete m_process;
    else
      m_process->Detach();
    m_process = nullptr;
  }
  AppendLog(wxString::Format("Exit status: %ld", exit_code));
  bool generation = m_processGeneration;
  bool cancelled = m_processCancelled;
  wxString command = m_currentCommand;
  m_processGeneration = false;
  m_processCancelled = false;
  m_currentCommand.clear();
  m_processRunning = false;
  m_processPid = 0;
  SetBusy(false);

  wxString nativeError;
  wxString extensionSummary;
  wxJSONValue completedResult;
  if (!m_resultPath.empty() && wxFileName::FileExists(m_resultPath)) {
    wxFile resultFile(m_resultPath);
    wxString resultText;
    if (resultFile.IsOpened() && resultFile.ReadAll(&resultText)) {
      wxJSONValue resultValue;
      wxJSONReader reader;
      if (reader.Parse(resultText, &resultValue) == 0) {
        completedResult = resultValue;
        const auto warnings = resultValue["result"]["diagnostics"]["warnings"];
        for (int i = 0; i < warnings.Size(); ++i)
          AppendLog("Forecast note: " + warnings.ItemAt(i).AsString());
        if (resultValue.HasMember("error") &&
            resultValue["error"].HasMember("message")) {
          nativeError = resultValue["error"]["message"].AsString();
          AppendLog("Native generator error: " + Redact(nativeError));
        }
        wxJSONValue coverage =
            resultValue["result"]["diagnostics"]["forecast_extension"]["coverage"];
        for (const wxString& component :
             {wxString("weather"), wxString("waves"), wxString("current")}) {
          wxJSONValue entries = coverage[component];
          if (!entries.IsArray()) continue;
          for (int i = 0; i < entries.Size(); ++i) {
            wxJSONValue entry = entries.ItemAt(i);
            if (!extensionSummary.empty()) extensionSummary += "\n";
            extensionSummary +=
                component + ": " + entry["role"].AsString() + " " +
                entry["source"].AsString() + " through " +
                wxString::Format("%dh", entry["through_hour"].AsInt()) + " (" +
                entry["status"].AsString() + ")";
          }
        }
      }
    }
    // Windows does not allow removing an open file.  Close the result before
    // cleaning up; POSIX previously masked this ordering bug.
    if (resultFile.IsOpened()) resultFile.Close();
    wxRemoveFile(m_resultPath);
    m_resultPath.clear();
  }
  if (!m_jobPath.empty()) {
    wxRemoveFile(m_jobPath);
    m_jobPath.clear();
  }

  if (!launched) {
    wxMessageBox(
        "The generator process failed to launch. Check the generator "
        "executable path.",
        "Launch failed", wxOK | wxICON_ERROR, this);
    return;
  }
  wxString validationDetails;
  bool outputValid = generation && OutputFileLooksValidGrib(&validationDetails);
  if (outputValid && exit_code != 0) {
    AppendLog(validationDetails);
  }
  bool generationSucceeded =
      generation && (exit_code == 0 || (exit_code < 0 && outputValid));
  if (cancelled && !generationSucceeded) {
    AppendLog("Process cancelled.");
    return;
  }
  if (generationSucceeded) {
    if (m_estimateProcess || m_estimateTimer.IsRunning())
      m_estimateDecoded = _("Decoded forecast data: not yet known");
    StopEstimate();
    double actualNumericMiB = 0;
    if (ReadGeneratorActualNumericMiB(completedResult, &actualNumericMiB))
      m_estimateDecoded = wxString::Format(
          _("Decoded numeric data in generated file: %.2f MiB"), actualNumericMiB);
    else
      m_estimateDecoded = _("Decoded numeric data in generated file: not yet known");
    const double mib = wxFileName(OutputPath()).GetSize().ToDouble() / 1048576.0;
    ShowEstimate(wxString::Format(_("Actual GRIB file: %.2f MiB"), mib),
                 m_estimateDecoded);
    m_estimateSummary->UnsetToolTip();
    wxJSONValue report = completedResult["result"]["size_comparison"];
    if (completedResult["status"].AsString() == "complete" &&
        report["schemaVersion"].IsInt() && report["schemaVersion"].AsInt() == 1) {
      const wxString reportPath = OutputPath() + ".size-report.json";
      wxString reportText;
      wxJSONWriter writer;
      writer.Write(report, reportText);
      // wxTempFile stages beside the destination then commits the complete
      // report. A reporting failure must not discard a valid generated GRIB.
      wxTempFile reportFile(reportPath);
      if (reportFile.IsOpened() && reportFile.Write(reportText) && reportFile.Commit())
        AppendLog("Size comparison saved: " + reportPath +
                  "\nContains area/time and settings, but no credentials or source paths.");
      else
        AppendLog("Could not save size comparison; the generated GRIB is unaffected.");
      m_estimateSummary->SetToolTip(
          _("Final inventory includes all generated fields, levels, times and missing cells.\n"
            "A size comparison report is saved beside the GRIB when possible.\n"
            "It contains area/time and settings; review before sharing."));
    }
    wxString message =
        "Generated environmental GRIB\nSource: " + SourceLabel() +
        "\nValid time: " + ValidTimeSummary() +
        "\nMessages: see validation summary in log" +
        "\nOutput: " + OutputPath();
    if (!extensionSummary.empty())
      message += "\n\nActual forecast composition:\n" + extensionSummary;
    auto timeCoverage = completedResult["result"]["diagnostics"]["time_coverage"];
    const auto timePolicy = timeCoverage["policy"].AsString();
    if (timeCoverage["severity"].AsString() == "advisory" &&
        (timePolicy == "review" || timePolicy == "keep-all"))
      message += "\n\nCoverage note:\n" + timeCoverage["message"].AsString() +
                 "\n" + timeCoverage["coverageSummary"].AsString();
    if (m_openAfter->GetValue()) {
      TryOpenGeneratedGrib();
      message += "\n\nThe generated file was opened in Weather Routing.";
    } else {
      message +=
          "\n\nUse Load GRIB in Weather Routing to select this file. It is already a "
          "merged environmental GRIB when both weather and currents were "
          "selected.";
    }
    AppendLog(message);
    wxMessageBox(message, "Environmental GRIB generated",
                 wxOK | wxICON_INFORMATION, this);
  } else if (exit_code != 0 && generation) {
    if (command.Contains("--use-source-grid")) {
      AppendLog(
          "If this failed while using the NetCDF source grid, retry from the "
          "CLI without --use-source-grid to interpolate to a regular grid.");
    }
    if (completedResult["error"]["code"].AsString() == "preflight_required") {
      wxString issue;
      wxJSONWriter writer;
      writer.Write(completedResult["error"]["preflight"], issue);
      if (ChooseTimeOptions(issue)) {
        CallAfter([this] { wxCommandEvent event; OnGenerate(event); });
      }
      return;
    }
    // Completion has already released the process and enabled Close. Keep
    // failures in this window: a native modal message containing a long
    // download URL can put its dismissal button outside the display and
    // prevent the generator window from closing.
    if (nativeError.empty())
      AppendLog("Environmental GRIB generation failed. See the command output above.");
    else
      wxLogMessage("xGRIB environmental generation failed: %s", Redact(nativeError));
    if (nativeError.Contains("Copernicus NWS dataset") && nativeError.Contains("time"))
      m_processStatus->SetLabel(_("Current data do not cover the requested times.\nReview the UTC start and duration, or choose another current source."));
    else
      m_processStatus->SetLabel(_("Generation failed. Details are in the log above."));
    m_closeButton->SetFocus();
  }
}

bool EnvironmentalGribDialog::ChildProcessStillExists() const {
  if (!m_processRunning || m_processPid == 0) return false;
#ifdef __UNIX__
  errno = 0;
  if (kill(static_cast<pid_t>(m_processPid), 0) == 0) return true;
  if (errno == ESRCH) return false;
  return true;
#else
  return wxProcess::Exists(static_cast<int>(m_processPid));
#endif
}

bool EnvironmentalGribDialog::OutputFileLooksValidGrib(
    wxString* details) const {
  wxString path = OutputPath();
  if (!wxFileName::FileExists(path)) {
    if (details) *details = "output file does not exist";
    return false;
  }
  return GribStreamIsStrictlyValid(path, details);
}

void EnvironmentalGribDialog::SetBusy(bool busy) {
  m_processStatus->SetLabel(busy ? _("Starting helper...") : _("Ready"));
  m_checkButton->Enable(!busy);
  m_generateButton->Enable(!busy);
  m_cancelButton->Enable(busy);
  m_closeButton->Enable(true);
  UpdateProviderUi();
}

void EnvironmentalGribDialog::TryOpenGeneratedGrib() {
  wxString path = OutputPath();
  if (!wxFileName::FileExists(path)) {
    AppendLog("Generated GRIB does not exist; it cannot be opened.");
    return;
  }
  if (m_onGribReady) {
    m_onGribReady(path);
    AppendLog("Opened generated GRIB in Weather Routing: " + path);
    wxLogMessage("xGRIB: opened generated GRIB: %s", path);
    return;
  }

  // Retain the standard GRIB message as a compatibility fallback for builds
  // embedding this dialog outside xGRIB.
  const wxString body = "{\"grib_file\":\"" + JsonEscape(path) + "\"}";
  SendPluginMessage("GRIB_APPLY_JSON_CONFIG", body);
  AppendLog("Requested GRIB open through plugin messaging: " + path);
}

wxString EnvironmentalGribDialog::SelectedCurrentSource() const {
  wxString currentSource = "none";
  if (m_generateCurrents->GetValue()) {
    const wxString selected = m_currentSource->GetStringSelection();
    if (IsOfflineTidalSelected())
      currentSource = "offline-tidal";
    else if (selected.Contains("TPXO cache"))
      currentSource = "tpxo-cache";
    else if (selected.Contains("TPXO direct"))
      currentSource = "tpxo";
    else if (selected.Contains("Existing"))
      currentSource = "existing-file";
    else if (selected.Contains("Marine.ie"))
      currentSource = "marine_ie_irish_sea";
    else if (selected.Contains("RTOFS"))
      currentSource = "noaa_rtofs_global";
    else if (selected.Contains("OFS"))
      currentSource = "noaa_ofs_s111";
    else if (selected.Contains("NWS"))
      currentSource = "copernicus_nws";
    else if (selected.Contains("IBI"))
      currentSource = "copernicus_ibi";
    else if (selected.Contains("Mediterranean"))
      currentSource = "copernicus_mediterranean";
    else if (selected.Contains("Global"))
      currentSource = "copernicus_global";
    else if (selected.Contains("Auto"))
      currentSource = "auto";
  }
  if (m_generateCurrents->GetValue() && !IsOfflineTidalSelected()) {
    if (m_mode->GetSelection() == 2) currentSource = "netcdf";
    if (m_mode->GetSelection() == 3) currentSource = "synthetic";
  }

  return currentSource;
}

std::optional<weather_routing::PassageWeatherWindow>
EnvironmentalGribDialog::DownloadWindow() const {
  return weather_routing::BuildWeatherDownloadWindow(
      DisplayedStartUtc(), m_durationHours->GetValue(), m_stepHours->GetValue(),
      m_includeWaves->GetValue(), SelectedCurrentSource().ToStdString());
}

bool EnvironmentalGribDialog::BuildGenerateJobText(wxString* text,
                                                  wxString* error) const {
  wxTextCtrl* controls[] = {m_west, m_south, m_east, m_north};
  double bounds[4]{};
  if (!ReadAreaCoordinates(controls, bounds, error)) return false;
  wxString timeError;
  if (!m_startUtc->GetUtc(&timeError).IsValid()) {
    if (error) *error = timeError;
    return false;
  }
  const double west = bounds[0], south = bounds[1], east = bounds[2], north = bounds[3];
  double currentSpacing = 0.0;
  if (!m_tpxoGridSpacing->GetValue().ToDouble(&currentSpacing)) {
    if (error) *error = "grid spacing must be numeric";
    return false;
  }

  wxString weatherProvider = "none";
  if (m_generateWeather->GetValue()) {
    const wxString selected = m_weatherProvider->GetStringSelection();
    if (selected.Contains("NOAA GFS"))
      weatherProvider = "gfs";
    else if (selected.Contains("HRRR"))
      weatherProvider = "noaa_hrrr";
    else if (selected.Contains("Met Office UKV"))
      weatherProvider = "ukmo_ukv";
    else if (selected.Contains("MET Norway"))
      weatherProvider = "metno_nordic";
    else if (selected.Contains("ICON-EU"))
      weatherProvider = "dwd_icon_eu";
    else if (selected.Contains("AIFS"))
      weatherProvider = "ecmwf_aifs_open";
    else if (selected.Contains("ECMWF"))
      weatherProvider = "ecmwf_ifs_open";
    else if (selected.Contains("Existing"))
      weatherProvider = "existing-file";
  }
  wxString weatherPreset = "routing";
  if (m_weatherPreset->GetStringSelection().Contains("Minimal")) {
    weatherPreset = "minimal";
  } else if (m_weatherPreset->GetStringSelection().Contains("Marine")) {
    weatherPreset = "marine";
  } else if (m_weatherPreset->GetStringSelection().Contains("All available")) {
    weatherPreset = "all";
  }

  const wxString currentSource = SelectedCurrentSource();

  wxJSONValue root = CreateGeneratorJobEnvelope();
  wxJSONValue& request = root["request"];
  request["bbox"]["west"] = west;
  request["bbox"]["south"] = south;
  request["bbox"]["east"] = east;
  request["bbox"]["north"] = north;
  const auto window = DownloadWindow();
  if (!window) {
    if (error)
      *error = _("Review the download start, duration and forecast interval. The time must exist in the selected time zone.");
    return false;
  }
  request["start"] = window->start.Format("%Y-%m-%dT%H:%M:%SZ", wxDateTime::UTC);
  request["hours"] = window->hours;
  request["stepHours"] = m_stepHours->GetValue();
  request["timePolicy"] = m_timePolicy;
  request["weatherProvider"] = weatherProvider;
  request["extendForecast"] = m_extendForecast->GetValue();
  request["fallbackWeatherProvider"] = wxString(
      m_extendForecast->GetValue() &&
              m_fallbackWeatherProvider->GetStringSelection().Contains("GFS")
          ? "gfs"
          : "none");
  request["fallbackWaveProvider"] = wxString(
      m_extendForecast->GetValue() &&
              m_fallbackWaveProvider->GetStringSelection().Contains("GFS")
          ? "gfs_wave"
          : "none");
  request["fallbackCurrentSource"] = wxString(
      m_extendForecast->GetValue() &&
              m_fallbackCurrentSource->GetStringSelection().Contains("Offline")
          ? "offline-tidal"
          : "none");
  request["weatherPreset"] = weatherPreset;
  request["weatherGridSpacingDeg"] = 0.025;
  request["weatherFile"] = m_existingWeatherFile->GetPath();
  request["includeWaves"] = m_includeWaves->GetValue();
  request["waveProvider"] =
      wxString(m_waveProvider->GetStringSelection().Contains("Copernicus")
                   ? "copernicus_global_waves"
                   : "gfs_wave");
  request["waveStepHours"] = 3;
  request["currentSource"] = currentSource;
  request["currentFile"] = m_existingCurrentFile->GetPath();
  request["offlineTidalFile"] = m_offlineTidalFile->GetPath();
  request["offlineCurrentMode"] = wxString(
      m_offlineCurrentMode->GetSelection() == 1 ? "tide-expected-seasonal"
                                                : "tide-only");
  request["inputNetcdf"] = m_localNetcdf->GetPath();
  request["inputCache"] = m_tpxoCacheFile->GetPath();
  request["tpxoModelDirectory"] = m_tpxoModelDir->GetPath();
  request["autoPrepareTpxoCache"] = currentSource == "tpxo-cache";
  request["currentGridSpacingDeg"] = currentSpacing;
  request["inferMinorTides"] = true;
  request["output"] = OutputPath();
  request["overwrite"] = true;
  request["keepIntermediate"] = false;
  request["dryRun"] = false;
  if (NeedsCopernicusCredentials() || currentSource == "noaa_rtofs_global") {
    wxFileName downloadDir;
    downloadDir.AssignDir(m_outputDir->GetPath());
    downloadDir.AppendDir("currentgrib_downloads");
    request["downloadDirectory"] = downloadDir.GetPath();
  }
  if (NeedsCopernicusCredentials()) {
    request["copernicusUsername"] = m_username->GetValue();
  }
  wxJSONWriter writer;
  writer.Write(root, *text);
  return true;
}

bool EnvironmentalGribDialog::WriteGenerateJob(const wxString& job_path,
                                               wxString* error) const {
  wxString text;
  if (!BuildGenerateJobText(&text, error)) return false;
  wxFile file(job_path, wxFile::write);
  if (!file.IsOpened() || !file.Write(text)) {
    if (error) *error = "cannot write " + job_path;
    return false;
  }
  return true;
}

wxString EnvironmentalGribDialog::BuildGenerateCommand() {
  const wxString token = wxString::Format(
      "%ld-%s", wxGetProcessId(), wxDateTime::Now().Format("%Y%m%d%H%M%S"));
  wxFileName job(m_outputDir->GetPath(),
                 ".environmental-grib-job-" + token + ".json");
  wxFileName result(m_outputDir->GetPath(),
                    ".environmental-grib-result-" + token + ".json");
  m_jobPath = job.GetFullPath();
  m_resultPath = result.GetFullPath();
  wxString error;
  if (!WriteGenerateJob(m_jobPath, &error)) {
    AppendLog("Cannot create native generator job: " + error);
    m_jobPath.clear();
    m_resultPath.clear();
    return {};
  }
  return xgrib::QuoteProcessArgument(m_generatorPath->GetValue()) +
         " run-job --job " + xgrib::QuoteProcessArgument(m_jobPath) +
         " --result " + xgrib::QuoteProcessArgument(m_resultPath);
}

wxString EnvironmentalGribDialog::OutputPath() const {
  wxFileName output(m_outputDir->GetPath(), m_outputFile->GetValue());
  return output.GetFullPath();
}

wxString EnvironmentalGribDialog::SourceLabel() const {
  wxString weather = m_generateWeather->GetValue()
                         ? m_weatherProvider->GetStringSelection()
                         : wxString("None");
  wxString current = m_generateCurrents->GetValue()
                         ? m_currentSource->GetStringSelection()
                         : wxString("None");
  wxString waves = "None";
  if (m_includeWaves->GetValue()) {
    waves = m_waveProvider->GetStringSelection();
  }
  wxString label = "Environmental GRIB: weather=" + weather +
                   ", currents=" + current + ", waves=" + waves;
  if (m_extendForecast->GetValue()) {
    label += "; long-range weather=" +
             m_fallbackWeatherProvider->GetStringSelection() +
             ", waves=" + m_fallbackWaveProvider->GetStringSelection() +
             ", currents=" + m_fallbackCurrentSource->GetStringSelection();
  }
  return label;
}

wxString EnvironmentalGribDialog::ValidTimeSummary() const {
  const auto window = DownloadWindow();
  if (window) {
    const wxDateTime start = window->start;
    const wxDateTime end = start + wxTimeSpan::Hours(window->hours);
    const wxString zone =
        m_useLocalTimeZone ? m_displayTimeZone : wxString("UTC");
    const auto format = [&zone](const wxDateTime& value) {
      const wxDateTime wall = marine_time::ToWallClock(value, zone);
      return wall.Format("%d %b %Y, %H:%M", wxDateTime::UTC) + " " +
             marine_time::TimeZoneAbbreviation(value, zone);
    };
    return format(start) + " to " + format(end);
  }
  return _("Review the start date/time") + " plus " +
         wxString::Format("%d hours", m_durationHours->GetValue());
}

wxDateTime EnvironmentalGribDialog::DisplayedStartUtc() const {
  return m_startUtc->GetUtc();
}

wxString EnvironmentalGribDialog::StartUtcText() const {
  const wxDateTime utc = DisplayedStartUtc();
  if (!utc.IsValid()) return wxEmptyString;
  return utc.Format("%Y-%m-%dT%H:%M:%SZ", wxDateTime::UTC);
}

void EnvironmentalGribDialog::SetDisplayTimeZone(
    bool enabled, const wxString& zoneName) {
  m_useLocalTimeZone =
      enabled && marine_time::IsTimeZoneAvailable(zoneName);
  m_displayTimeZone = marine_time::IsTimeZoneAvailable(zoneName)
                          ? zoneName
                          : wxString("UTC");
  m_startUtc->SetDisplayTimeZone(m_useLocalTimeZone, m_displayTimeZone);
  Layout();
}

int EnvironmentalGribDialog::ExpectedMessageCount() const {
  int step = std::max(1, m_stepHours->GetValue());
  int timesteps = (m_durationHours->GetValue() / step) + 1;
  return timesteps * 2;
}

wxString EnvironmentalGribDialog::DefaultOutputFilenameForSelection() const {
  wxString prefix;
  bool weatherOn = m_generateWeather->GetValue() &&
                   m_weatherProvider->GetStringSelection() != "None";
  bool currentOn = m_generateCurrents->GetValue() &&
                   m_currentSource->GetStringSelection() != "None";
  bool wavesOn = m_includeWaves->GetValue();
  wxString weatherProvider = m_weatherProvider->GetStringSelection();
  wxString currentSource = m_currentSource->GetStringSelection();
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  bool looksIrishSea =
      m_west->GetValue().ToDouble(&west) &&
      m_south->GetValue().ToDouble(&south) &&
      m_east->GetValue().ToDouble(&east) &&
      m_north->GetValue().ToDouble(&north) && std::abs(west - -8.5) < 0.01 &&
      std::abs(south - 50.5) < 0.01 && std::abs(east - -2.5) < 0.01 &&
      std::abs(north - 56.5) < 0.01;
  bool looksGulfStream = m_west->GetValue().ToDouble(&west) &&
                         m_south->GetValue().ToDouble(&south) &&
                         m_east->GetValue().ToDouble(&east) &&
                         m_north->GetValue().ToDouble(&north) &&
                         west >= -85.0 && east <= -60.0 && south >= 20.0 &&
                         north <= 42.0;
  bool ukvMixedCadence = weatherProvider.Contains("UKV") &&
                         m_stepHours->GetValue() == 1 &&
                         m_durationHours->GetValue() > 54;
  if ((weatherOn || wavesOn) && currentOn) {
    prefix = "environment";
    if (weatherOn) {
      if (weatherProvider.Contains("HRRR"))
        prefix += "_hrrr";
      else if (weatherProvider.Contains("GFS"))
        prefix += "_gfs";
      else if (weatherProvider.Contains("UKV"))
        prefix += "_ukmo_ukv";
      else if (weatherProvider.Contains("MET Norway"))
        prefix += "_metno";
      else if (weatherProvider.Contains("ICON-EU"))
        prefix += "_icon_eu";
      else if (weatherProvider.Contains("AIFS"))
        prefix += "_ecmwf_aifs";
      else if (weatherProvider.Contains("ECMWF"))
        prefix += "_ecmwf_ifs";
      else if (weatherProvider.Contains("Existing"))
        prefix += "_existing_weather";
    }
    if (ukvMixedCadence) prefix += "_mixed";
    if (wavesOn) {
      prefix += m_waveProvider->GetStringSelection().Contains("Copernicus")
                    ? "_copernicus_waves"
                    : "_wave";
    }
    if (IsOfflineTidalSelected())
      prefix += "_offline_tidal";
    else if (currentSource.Contains("TPXO cache"))
      prefix += "_tpxo_cache";
    else if (currentSource.Contains("TPXO direct"))
      prefix += "_tpxo";
    else if (currentSource.Contains("Marine.ie"))
      prefix += "_marine_ie";
    else if (currentSource.Contains("RTOFS"))
      prefix += "_noaa_rtofs_global";
    else if (currentSource.Contains("OFS"))
      prefix += "_noaa_ofs_s111";
    else if (currentSource.Contains("NWS"))
      prefix += "_copernicus_nws";
    else if (currentSource.Contains("IBI"))
      prefix += "_copernicus_ibi";
    else if (currentSource.Contains("Mediterranean"))
      prefix += "_copernicus_mediterranean";
    else if (currentSource.Contains("Global"))
      prefix += "_copernicus_global";
    else if (currentSource.Contains("Auto"))
      prefix += "_auto_current";
    else if (currentSource.Contains("Existing"))
      prefix += "_existing_current";
    if (looksIrishSea) prefix += "_irish_sea";
    if (currentSource.Contains("RTOFS") && looksGulfStream)
      prefix += "_gulf_stream";
  } else if (weatherOn || wavesOn) {
    prefix = weatherOn ? "weather" : "waves";
    if (weatherOn) {
      if (weatherProvider.Contains("HRRR"))
        prefix += "_hrrr";
      else if (weatherProvider.Contains("GFS"))
        prefix += "_gfs";
      else if (weatherProvider.Contains("UKV"))
        prefix += "_ukmo_ukv";
      else if (weatherProvider.Contains("MET Norway"))
        prefix += "_metno";
      else if (weatherProvider.Contains("ICON-EU"))
        prefix += "_icon_eu";
      else if (weatherProvider.Contains("AIFS"))
        prefix += "_ecmwf_aifs";
      else if (weatherProvider.Contains("ECMWF"))
        prefix += "_ecmwf_ifs";
      else
        prefix += "_existing";
    }
    if (ukvMixedCadence) prefix += "_mixed";
    if (weatherOn && m_weatherPreset->GetStringSelection().Contains("Marine"))
      prefix += "_marine";
    if (weatherOn &&
        m_weatherPreset->GetStringSelection().Contains("All available"))
      prefix += "_all";
    if (wavesOn) {
      prefix += m_waveProvider->GetStringSelection().Contains("Copernicus")
                    ? "_copernicus_waves"
                    : "_wave";
    }
    if (looksIrishSea) prefix += "_irish_sea";
  } else if (currentOn) {
    prefix = "current";
    if (IsOfflineTidalSelected())
      prefix += "_offline_tidal";
    else if (currentSource.Contains("TPXO cache"))
      prefix += "_tpxo_cache";
    else if (currentSource.Contains("TPXO direct"))
      prefix += "_tpxo";
    else if (currentSource.Contains("Marine.ie"))
      prefix += "_marine_ie";
    else if (currentSource.Contains("RTOFS"))
      prefix += "_noaa_rtofs_global";
    else if (currentSource.Contains("OFS"))
      prefix += "_noaa_ofs_s111";
    else if (currentSource.Contains("NWS"))
      prefix += "_copernicus_nws";
    else if (currentSource.Contains("IBI"))
      prefix += "_copernicus_ibi";
    else if (currentSource.Contains("Mediterranean"))
      prefix += "_copernicus_mediterranean";
    else if (currentSource.Contains("Global"))
      prefix += "_copernicus_global";
    else if (currentSource.Contains("Auto"))
      prefix += "_auto";
    else
      prefix += "_existing";
    if (currentSource.Contains("RTOFS") && looksGulfStream)
      prefix += "_gulf_stream";
  }
  if (!prefix.empty()) return TimestampedFilename(prefix);

  int mode = m_mode->GetSelection();
  if (mode == 1) {
    return looksIrishSea ? IrishSeaTpxoOutputFilename()
                         : DefaultTpxoOutputFilename();
  }
  if (mode == 2) return TimestampedFilename("local_netcdf_current");
  if (mode == 3) return TimestampedFilename("synthetic_current");

  wxString provider = m_provider->GetStringSelection();
  if (IsMarineIeProvider(provider) ||
      (provider == "Auto" && AutoWouldUseMarineIe())) {
    return MarineIeOutputFilename();
  }
  if (provider.Contains("Copernicus Marine Global")) {
    return TimestampedFilename("copernicus_global_current");
  }
  if (provider.Contains("Copernicus Marine North-West Shelf")) {
    return TimestampedFilename("copernicus_nws_current");
  }
  if (provider == "Auto") {
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;
    bool parsed = m_west->GetValue().ToDouble(&west) &&
                  m_south->GetValue().ToDouble(&south) &&
                  m_east->GetValue().ToDouble(&east) &&
                  m_north->GetValue().ToDouble(&north);
    if (parsed && west < east && west >= -20.0 && east <= 13.0 && south >= 40.0 &&
        north <= 65.0) {
      return TimestampedFilename("copernicus_nws_current");
    }
    return TimestampedFilename("copernicus_global_current");
  }
  return TimestampedFilename("current_grib");
}

void EnvironmentalGribDialog::RefreshOutputFilenameDefault() {
  QueueEstimate();
  wxString previousAuto = m_lastAutoOutputFilename;
  wxString current = m_outputFile->GetValue();
  wxString nextAuto = DefaultOutputFilenameForSelection();
  bool shouldUpdate = current.empty() || !m_outputFileUserCustomized ||
                      (!previousAuto.empty() && current == previousAuto);
  m_lastAutoOutputFilename = nextAuto;
  if (!shouldUpdate) return;
  m_updatingOutputFilename = true;
  m_outputFile->SetValue(nextAuto);
  m_updatingOutputFilename = false;
  m_outputFileUserCustomized = false;
}

void EnvironmentalGribDialog::LoadSettings() {
  wxConfigBase* config = GetOCPNConfigObject();
  if (!config) return;
  wxString oldPath = config->GetPath();
  config->SetPath("/PlugIns/xWeatherRouting2/EnvironmentalGenerator");
  long mode = config->ReadLong("generation_mode", m_mode->GetSelection());
  if (mode >= 0 && mode < static_cast<long>(m_mode->GetCount())) {
    m_mode->SetSelection(static_cast<int>(mode));
  }
  wxString value;
  if (config->Read("tpxo_model_directory", &value) && !value.empty()) {
    m_tpxoModelDir->SetPath(value);
  }
  if (config->Read("tpxo_model_name", &value) && !value.empty()) {
    m_tpxoModelName->SetValue(value);
  }
  if (config->Read("tpxo_grid_spacing", &value) && !value.empty()) {
    m_tpxoGridSpacing->SetValue(value);
  }
  if (config->Read("tpxo_cache_file", &value) && !value.empty()) {
    m_tpxoCacheFile->SetPath(value);
  }
  if (config->Read("offline_tidal_file", &value) && !value.empty()) {
    m_offlineTidalFile->SetPath(value);
  }
  const long offlineCurrentMode = config->ReadLong("offline_current_mode", 0);
  m_offlineCurrentMode->SetSelection(offlineCurrentMode == 1 ? 1 : 0);
  const long currentSourceSelection = config->ReadLong(
      "current_source_selection", m_currentSource->GetSelection());
  if (currentSourceSelection >= 0 &&
      currentSourceSelection < static_cast<long>(m_currentSource->GetCount())) {
    m_currentSource->SetSelection(static_cast<int>(currentSourceSelection));
  }
  m_useTpxoCache->SetValue(config->ReadBool("use_tpxo_cache", false));
  m_durationHours->SetValue(static_cast<int>(
      config->ReadLong("duration_hours", m_durationHours->GetValue())));
  m_stepHours->SetValue(static_cast<int>(
      config->ReadLong("step_hours", m_stepHours->GetValue())));
  m_extendForecast->SetValue(
      config->ReadBool("extend_forecast", m_extendForecast->GetValue()));
  const long fallbackWeather = config->ReadLong(
      "fallback_weather_selection", m_fallbackWeatherProvider->GetSelection());
  if (fallbackWeather >= 0 &&
      fallbackWeather <
          static_cast<long>(m_fallbackWeatherProvider->GetCount()))
    m_fallbackWeatherProvider->SetSelection(static_cast<int>(fallbackWeather));
  const long fallbackWave = config->ReadLong(
      "fallback_wave_selection", m_fallbackWaveProvider->GetSelection());
  if (fallbackWave >= 0 &&
      fallbackWave < static_cast<long>(m_fallbackWaveProvider->GetCount()))
    m_fallbackWaveProvider->SetSelection(static_cast<int>(fallbackWave));
  const long fallbackCurrent = config->ReadLong(
      "fallback_current_selection", m_fallbackCurrentSource->GetSelection());
  if (fallbackCurrent >= 0 &&
      fallbackCurrent < static_cast<long>(m_fallbackCurrentSource->GetCount()))
    m_fallbackCurrentSource->SetSelection(static_cast<int>(fallbackCurrent));
  bool rememberUsername =
      config->ReadBool("remember_copernicus_username", false);
  m_rememberUsername->SetValue(rememberUsername);
  if (rememberUsername && config->Read("copernicus_username", &value)) {
    m_username->SetValue(value);
  }
  config->SetPath(oldPath);
}

void EnvironmentalGribDialog::SaveSettings() {
  wxConfigBase* config = GetOCPNConfigObject();
  if (!config) return;
  wxString oldPath = config->GetPath();
  config->SetPath("/PlugIns/xWeatherRouting2/EnvironmentalGenerator");
  config->Write("generation_mode", static_cast<long>(m_mode->GetSelection()));
  config->Write("tpxo_model_directory", m_tpxoModelDir->GetPath());
  config->Write("tpxo_model_name", m_tpxoModelName->GetValue());
  config->Write("tpxo_grid_spacing", m_tpxoGridSpacing->GetValue());
  config->Write("tpxo_cache_file", m_tpxoCacheFile->GetPath());
  config->Write("offline_tidal_file", m_offlineTidalFile->GetPath());
  config->Write("offline_current_mode",
                static_cast<long>(m_offlineCurrentMode->GetSelection()));
  config->Write("current_source_selection",
                static_cast<long>(m_currentSource->GetSelection()));
  config->Write("use_tpxo_cache", m_useTpxoCache->GetValue());
  config->Write("duration_hours",
                static_cast<long>(m_durationHours->GetValue()));
  config->Write("step_hours", static_cast<long>(m_stepHours->GetValue()));
  config->Write("extend_forecast", m_extendForecast->GetValue());
  config->Write("fallback_weather_selection",
                static_cast<long>(m_fallbackWeatherProvider->GetSelection()));
  config->Write("fallback_wave_selection",
                static_cast<long>(m_fallbackWaveProvider->GetSelection()));
  config->Write("fallback_current_selection",
                static_cast<long>(m_fallbackCurrentSource->GetSelection()));
  config->Write("remember_copernicus_username", m_rememberUsername->GetValue());
  if (m_rememberUsername->GetValue()) {
    config->Write("copernicus_username", m_username->GetValue());
  } else {
    config->DeleteEntry("copernicus_username", false);
  }
  config->Flush();
  config->SetPath(oldPath);
}

wxString EnvironmentalGribDialog::FindDefaultGenerator() const {
  wxString path;
  if (wxGetEnv("ENVIRONMENTAL_GRIB_GENERATOR", &path) &&
      IsExecutableFile(path)) {
    return path;
  }

  wxFileName packaged(GetXgribDataDirectory(), "");
  packaged.RemoveLastDir();
  packaged.AppendDir("bin");
#ifdef __WXMSW__
  packaged.SetFullName("environmental-grib.exe");
#else
  packaged.SetFullName("environmental-grib");
#endif
  if (IsExecutableFile(packaged.GetFullPath())) return packaged.GetFullPath();

  // A source-build directory may contain an older development helper.  The
  // helper shipped with this plugin must take precedence so its job schema
  // and provider set always match the loaded UI.
  wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
#ifdef __WXMSW__
  wxFileName sibling(executable.GetPath(), "environmental-grib.exe");
#else
  wxFileName sibling(executable.GetPath(), "environmental-grib");
#endif
  if (IsExecutableFile(sibling.GetFullPath())) return sibling.GetFullPath();

  if (wxFindFileInPath(&path, wxGetenv("PATH"), "environmental-grib")) {
    return path;
  }
  return "environmental-grib";
}

wxString EnvironmentalGribDialog::Redact(const wxString& text) const {
  wxString redacted(text);
  if (!m_password->GetValue().empty())
    redacted.Replace(m_password->GetValue(), "<redacted>");
  if (!m_username->GetValue().empty())
    redacted.Replace(m_username->GetValue(), "<redacted-user>");
  RedactQueryParameter(&redacted, "x-cop-user");
  RedactQueryParameter(&redacted, "username");
  RedactQueryParameter(&redacted, "user");
  RedactQueryParameter(&redacted, "email");
  RedactQueryParameter(&redacted, "token");
  RedactQueryParameter(&redacted, "access_token");
  RedactQueryParameter(&redacted, "password");
  return redacted;
}
