// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from xGRIB 0.4.5 Android touch generator, source a5606ce.
#pragma once
#include <functional>
#include <memory>
#include <wx/dialog.h>
#include "ocpn_plugin.h"

int WR_ChooseForecastTime(wxWindow* parent, const wxArrayString& times, int selected);
void WR_FitForecastDialog(wxWindow* parent, wxDialog& dialog);

class AndroidPassageWeatherDialog : public wxDialog {
 public:
  using GribReadyCallback = std::function<void(const wxString&)>;
  AndroidPassageWeatherDialog(wxWindow* parent, GribReadyCallback ready);
  ~AndroidPassageWeatherDialog() override;
  void ShowMobile(const PlugIn_ViewPort& viewport, bool serverDownload = false);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
