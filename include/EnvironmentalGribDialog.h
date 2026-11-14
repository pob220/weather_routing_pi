#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <wx/filepicker.h>
#include <wx/process.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include "ocpn_plugin.h"
#include "AreaPreset.h"
#include "PassageWeatherRequest.h"
class PlanningTimeCtrl;

class EnvironmentalGribDialog : public wxDialog {
public:
  using GribReadyCallback = std::function<void(const wxString&)>;

  explicit EnvironmentalGribDialog(wxWindow* parent,
                                   GribReadyCallback onGribReady = {});
  ~EnvironmentalGribDialog() override;
  void SetCurrentViewPort(const PlugIn_ViewPort& vp);
  void SetDisplayTimeZone(bool enabled, const wxString& zoneName);
  void PrepareForParentShutdown();
  void SetPassage(double west, double south, double east, double north,
                  const wxDateTime& start, int hours, bool currents, bool waves);
  void RunConfiguredSmokeTest();

private:
  void OnCheckDependencies(wxCommandEvent& event);
  void OnCheckTpxoModel(wxCommandEvent& event);
  void OnPrepareTpxoCache(wxCommandEvent& event);
  void OnGenerate(wxCommandEvent& event);
  bool ValidateAreaInput();
  bool ChooseTimeOptions(const wxString& issue_json);
  void OnBrowseOutput(wxCommandEvent& event);
  void OnOutputFilenameChanged(wxCommandEvent& event);
  void OnExistingGribFileChanged(wxFileDirPickerEvent& event);
  void OnOfflineTidalFileChanged(wxFileDirPickerEvent& event);
  void OnPresetChanged(wxCommandEvent& event);
  void OnManageAreaPresets(wxCommandEvent& event);
  void OnAreaCoordinateChanged(wxCommandEvent& event);
  void OnProviderChanged(wxCommandEvent& event);
  void OnModeChanged(wxCommandEvent& event);
  void OnCancel(wxCommandEvent& event);
  void OnClose(wxCommandEvent& event);
  void OnDialogClose(wxCloseEvent& event);
  void OnProcessTimer(wxTimerEvent& event);
  void OnProcessTerminated(wxProcessEvent& event);
  void QueueEstimate();
  void OnEstimateTimer(wxTimerEvent& event);
  void DrainEstimateOutput();
  void FinishEstimate(long exit_code);
  void StopEstimate();
  void ShowEstimate(const wxString& file, const wxString& decoded);
  void AppendLog(const wxString& message);
  void DrainProcessOutput();
  void FlushProcessOutput();
  void DrainStream(wxInputStream* stream, wxString* buffer,
                   const wxString& prefix);
  void StartCommand(const wxString& command, const wxString& password,
                    bool generation);
  void FinishCommand(long exit_code, bool launched,
                     bool completion_notified = false);
  bool ChildProcessStillExists() const;
  bool OutputFileLooksValidGrib(wxString* details = nullptr) const;
  void SetBusy(bool busy);
  void ApplyPreset(int selection);
  void PopulateAreaPresetChoice(const wxString& selected_id = {});
  xgrib::AreaPreset CurrentAreaBounds() const;
  std::optional<xgrib::AreaPreset> CurrentChartBounds() const;
  void ApplyProviderPreference(
      const wxString& provider_id,
      const std::vector<xgrib::ProviderPreferenceOption>& options,
      wxChoice* choice, wxCheckBox* enabled);
  bool ConfirmLargeCopernicusRequest();
  bool ValidateUkvRequest();
  bool ValidateMetNoRequest();
  bool AutoWouldUseMarineIe() const;
  bool NeedsCopernicusCredentials() const;
  bool IsOfflineTidalSelected() const;
  bool IsOfflineTidalNeeded() const;
  bool ValidateOfflineTidalPackage();
  void UpdateProviderUi();
  void RefreshOutputFilenameDefault();
  void LoadSettings();
  void ConfigureSmokeTestFromEnvironment();
  void UpdateSelectedPathDisplays();
  void SaveSettings();
  void TryOpenGeneratedGrib();
  wxString BuildGenerateCommand();
  bool WriteGenerateJob(const wxString& job_path, wxString* error) const;
  bool BuildGenerateJobText(wxString* text, wxString* error) const;
  wxString SelectedCurrentSource() const;
  std::optional<weather_routing::PassageWeatherWindow> DownloadWindow() const;
  wxString OutputPath() const;
  wxString SourceLabel() const;
  wxString ValidTimeSummary() const;
  wxDateTime DisplayedStartUtc() const;
  wxString StartUtcText() const;
  int ExpectedMessageCount() const;
  wxString DefaultOutputFilenameForSelection() const;
  wxString FindDefaultGenerator() const;
  wxString Redact(const wxString& text) const;

  wxTextCtrl* m_generatorPath;
  wxScrolledWindow* m_scrolled;
  wxTextCtrl* m_west;
  wxTextCtrl* m_south;
  wxTextCtrl* m_east;
  wxTextCtrl* m_north;
  PlanningTimeCtrl* m_startUtc;
  wxStaticText* m_startTimeLabel;
  wxSpinCtrl* m_durationHours;
  wxSpinCtrl* m_stepHours;
  wxCheckBox* m_extendForecast;
  wxStaticText* m_fallbackWeatherLabel;
  wxChoice* m_fallbackWeatherProvider;
  wxStaticText* m_fallbackWaveLabel;
  wxChoice* m_fallbackWaveProvider;
  wxStaticText* m_fallbackCurrentLabel;
  wxChoice* m_fallbackCurrentSource;
  wxCheckBox* m_generateWeather;
  wxChoice* m_weatherProvider;
  wxStaticText* m_weatherPresetLabel;
  wxChoice* m_weatherPreset;
  wxStaticText* m_wavesLabel;
  wxCheckBox* m_includeWaves;
  wxStaticText* m_waveProviderLabel;
  wxChoice* m_waveProvider;
  wxStaticText* m_existingWeatherFileLabel;
  wxFilePickerCtrl* m_existingWeatherFile;
  wxStaticText* m_existingWeatherPathLabel;
  wxTextCtrl* m_existingWeatherPath;
  wxCheckBox* m_generateCurrents;
  wxChoice* m_currentSource;
  wxStaticText* m_existingCurrentFileLabel;
  wxFilePickerCtrl* m_existingCurrentFile;
  wxStaticText* m_existingCurrentPathLabel;
  wxTextCtrl* m_existingCurrentPath;
  wxStaticText* m_offlineTidalFileLabel;
  wxFilePickerCtrl* m_offlineTidalFile;
  wxStaticText* m_offlineTidalStatusLabel;
  wxTextCtrl* m_offlineTidalStatus;
  wxStaticText* m_offlineCurrentModeLabel;
  wxChoice* m_offlineCurrentMode;
  wxChoice* m_mode;
  wxChoice* m_presetChoice;
  wxButton* m_manageAreaPresetsButton;
  wxChoice* m_provider;
  wxStaticText* m_usernameLabel;
  wxTextCtrl* m_username;
  wxStaticText* m_passwordLabel;
  wxTextCtrl* m_password;
  wxCheckBox* m_rememberUsername;
  wxStaticText* m_providerNote;
  wxStaticText* m_tpxoModelDirLabel;
  wxDirPickerCtrl* m_tpxoModelDir;
  wxStaticText* m_tpxoModelNameLabel;
  wxTextCtrl* m_tpxoModelName;
  wxStaticText* m_tpxoGridSpacingLabel;
  wxTextCtrl* m_tpxoGridSpacing;
  wxStaticText* m_checkTpxoLabel;
  wxButton* m_checkTpxoButton;
  wxCheckBox* m_useTpxoCache;
  wxStaticText* m_tpxoCacheFileLabel;
  wxFilePickerCtrl* m_tpxoCacheFile;
  wxStaticText* m_prepareTpxoCacheLabel;
  wxButton* m_prepareTpxoCacheButton;
  wxStaticText* m_localNetcdfLabel;
  wxFilePickerCtrl* m_localNetcdf;
  wxDirPickerCtrl* m_outputDir;
  wxTextCtrl* m_outputFile;
  wxButton* m_checkButton;
  wxButton* m_generateButton;
  wxButton* m_cancelButton;
  wxButton* m_closeButton;
  wxCheckBox* m_openAfter;
  wxCheckBox* m_showMergeInstructions;
  wxTextCtrl* m_log;
  wxStaticText* m_processStatus;
  wxTimer m_processTimer;
  wxTimer m_estimateTimer;
  wxStaticText* m_estimateSummary{nullptr};
  wxProcess* m_estimateProcess{nullptr};
  long m_estimatePid{0};
  unsigned long m_estimateRevision{0};
  unsigned long m_runningEstimateRevision{0};
  wxString m_estimateJobPath;
  wxString m_estimateOutput;
  wxString m_timeSettingsIssue;
  unsigned long m_timeSettingsRevision{0};
  wxString m_timePolicy{"review"};
  wxString m_estimateDecoded;
  wxString m_estimateFile;
  wxString m_lastEstimateRequest;
  wxLongLong m_estimateStarted;
  wxProcess* m_process{nullptr};
  bool m_processRunning{false};
  bool m_processGeneration{false};
  bool m_processCancelled{false};
  long m_processPid{0};
  wxLongLong m_processStarted;
  bool m_hasCurrentViewPort{false};
  PlugIn_ViewPort m_currentViewPort{};
  wxString m_currentCommand;
  wxString m_jobPath;
  wxString m_resultPath;
  wxString m_stdoutBuffer;
  wxString m_stderrBuffer;
  wxString m_lastAutoOutputFilename;
  GribReadyCallback m_onGribReady;
  bool m_outputFileUserCustomized{false};
  bool m_updatingOutputFilename{false};
  bool m_applyingAreaPreset{false};
  std::vector<xgrib::AreaPreset> m_areaPresets;
  bool m_offlineTidalPackageValid{false};
  bool m_offlineClimatologyAvailable{false};
  bool m_smokeTestConfigured{false};
  bool m_useLocalTimeZone{false};
  wxString m_displayTimeZone{"UTC"};
};
