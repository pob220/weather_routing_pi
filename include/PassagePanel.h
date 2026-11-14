// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/panel.h>
#include <wx/timer.h>
#include "Boat.h"
#include "PassageReadiness.h"
class WeatherRouting;
class EnvironmentalGribDialog;
class wxChoice;
class wxStaticText;
class wxButton;
class wxScrolledWindow;
class wxSizer;
class RouteMapOverlay;
struct RouteMapConfiguration;

class PassagePanel : public wxPanel {
public:
  PassagePanel(wxWindow* parent, WeatherRouting& routing);
  ~PassagePanel() override;
  void PrepareForShutdown();
  void RefreshPassage();

private:
  void EnsurePassage();
  void SelectPassage(int index);
  void EditRoute();
  void EditRouteWaypoints();
  void NewPassage(bool example = false);
  void RestoreDefaults();
  void ChooseRoute();
  void ShowWaypoints();
  void ShowChecks();
  void ResolveReadinessIssue(const std::string& code);
  void ShowHelp();
  void OpenSettings(size_t section);
  void EditBoatDetails();
  void RefreshResults(RouteMapOverlay* current, bool busy);
  void ReviewFailure();
  RouteMapConfiguration PlanningConfiguration(
      const RouteMapConfiguration& selected) const;
  void ChooseBoat();
  void ShowLibrary();
  bool OpenWeather(const wxString& path, wxString& error);
  void LoadWeather();
  void GenerateWeather();
  void ApplyBoat(const wxString& path);
  WeatherRouting& routing_;
  wxChoice* passages_;
  wxChoice* windPolicy_;
  wxScrolledWindow* content_;
  wxSizer* resultsBox_;
  wxStaticText* results_;
  wxStaticText *route_, *boat_, *weather_, *limits_, *status_, *warnings_;
  wxButton *calculate_, *checksButton_, *failureButton_, *waypointFixButton_;
  wxButton *fixButton_, *drawSetupButton_, *exampleButton_;
  std::vector<wxWindow*> mutableControls_;
  weather_routing::PassageReadiness readiness_;
  RouteMapOverlay* resultRoute_{nullptr};
  wxString resultKey_, resultStatus_;
  bool climateWindReady_{false};
  wxTimer refresh_;
  Boat boatModel_;
  EnvironmentalGribDialog* generator_{nullptr};
};
