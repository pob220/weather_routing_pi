// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <wx/datetime.h>
#include <wx/panel.h>

class wxChoice;
class wxDatePickerCtrl;
class wxTimePickerCtrl;

wxDECLARE_EVENT(wxEVT_PLANNING_TIME_CHANGED, wxCommandEvent);

// Civil date/clock controls for planning. Stored instants and helper requests
// remain UTC; users never need to enter an ISO/Zulu timestamp.
class PlanningTimeCtrl : public wxPanel {
public:
  PlanningTimeCtrl(wxWindow* parent, const wxDateTime& utc,
                   const wxString& name, bool useLocal = false,
                   const wxString& preferredZone = "UTC");
  wxDateTime GetUtc(wxString* error = nullptr) const;
  void SetUtc(const wxDateTime& utc);
  void SetDisplayTimeZone(bool enabled, const wxString& zone);
  const wxString& TimeZone() const { return zone_; }

private:
  std::array<int, 6> Fields() const;
  void ChangeZone(const wxString& zone);
  void NotifyChanged();
  wxDatePickerCtrl* date_;
  wxTimePickerCtrl* clock_;
  wxChoice* zones_;
  wxString zone_{"UTC"}, savedZone_;
  wxDateTime savedUtc_;
  std::array<int, 6> savedFields_{};
};
