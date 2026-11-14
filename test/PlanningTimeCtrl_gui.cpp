// SPDX-License-Identifier: GPL-3.0-or-later
// Real widget checks: run on a disposable desktop/virtual display, not in the
// normal display-free CTest suite. No OpenCPN instance or profile is accessed.
#include "PlanningTimeCtrl.h"
#include "TimeZoneDisplay.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/datectrl.h>
#include <wx/frame.h>
#include <wx/timectrl.h>

class TimeTestApp : public wxApp {
public:
  bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(TimeTestApp);

namespace {
void Check(bool value, const char* text) {
  if (!value) throw std::runtime_error(text);
}
wxDateTime Utc(int y, unsigned m, unsigned d, int h, int minute, int second) {
  using namespace std::chrono;
  return wxDateTime(static_cast<time_t>(
      duration_cast<seconds>((sys_days{year{y}/month{m}/day{d}} +
          hours{h} + minutes{minute} + seconds{second}).time_since_epoch()).count()));
}
wxTimePickerCtrl* Clock(PlanningTimeCtrl* control) {
  return wxDynamicCast(wxWindow::FindWindowByName("Test clock", control), wxTimePickerCtrl);
}
}

int main(int argc, char** argv) {
  if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
  auto* frame = new wxFrame(nullptr, wxID_ANY, "Isolated planning-time tests");
  int status = 0;
  try {
    auto instant = Utc(2026, 7, 15, 12, 37, 29);
    instant.SetMillisecond(321);
    auto* control = new PlanningTimeCtrl(frame, instant, "Test", true, "Europe/London");
    Check(marine_time::IsTimeZoneAvailable("Europe/London"), "London timezone unavailable");
    Check(control->GetUtc() == instant, "Unedited date/clock changed the UTC instant");
    Check(Clock(control)->GetValue().GetHour() == 13, "Summer local clock did not show BST");
    control->SetDisplayTimeZone(false, "Europe/London");
    Check(control->GetUtc() == instant, "Switching to UTC changed the instant");
    Check(Clock(control)->GetValue().GetHour() == 12, "UTC clock fields were wrong");
    control->SetDisplayTimeZone(true, "Asia/Kathmandu");
    Check(control->GetUtc() == instant, "Quarter-hour offset changed the instant");
    Check(Clock(control)->GetValue().GetHour() == 18 &&
          Clock(control)->GetValue().GetMinute() == 22, "Quarter-hour local clock was wrong");
    control->SetDisplayTimeZone(true, "Europe/London");
    const auto repeated = Utc(2026, 10, 25, 1, 30, 47);
    control->SetUtc(repeated);
    Check(control->GetUtc() == repeated, "Unedited autumn time moved to its earlier occurrence");
    Clock(control)->SetValue(wxDateTime(15, wxDateTime::Jan, 2000, 1, 31, 47));
    wxString error;
    Check(!control->GetUtc(&error).IsValid() && error.Contains("occurs twice"),
          "Edited autumn ambiguity was silently accepted");
    // An explicit UTC selection lets the user resolve an ambiguous input.
    auto* zones = wxDynamicCast(wxWindow::FindWindowByName("Test time zone", control), wxChoice);
    zones->SetStringSelection("UTC");
    wxCommandEvent change(wxEVT_CHOICE, zones->GetId());
    change.SetEventObject(zones);
    zones->ProcessWindowEvent(change);
    Check(control->GetUtc() == Utc(2026, 10, 25, 1, 31, 47), "UTC selection did not resolve ambiguity");
    control->SetDisplayTimeZone(true, "Europe/London");
    control->SetUtc(Utc(2026, 3, 29, 0, 30, 0));
    Clock(control)->SetValue(wxDateTime(15, wxDateTime::Jan, 2000, 1, 30, 0));
    Check(!control->GetUtc(&error).IsValid() && error.Contains("clocks go forward"),
          "Nonexistent spring time was silently normalised");
    control->SetDisplayTimeZone(false, "UTC");
    const auto before = wxDateTime::Now();
    auto* now = wxDynamicCast(wxWindow::FindWindowByName("Test now", control), wxButton);
    wxCommandEvent press(wxEVT_BUTTON, now->GetId());
    press.SetEventObject(now);
    now->ProcessWindowEvent(press);
    const auto after = wxDateTime::Now();
    Check(control->GetUtc() >= before && control->GetUtc() <= after,
          "Now did not set the current instant");
    std::cout << "PASS: UTC/local round trips, seconds/milliseconds, quarter-hour offsets, "
                 "autumn occurrence preservation, explicit ambiguity resolution, spring rejection and Now\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    status = 1;
  }
  delete frame;
  wxTheApp->OnExit();
  wxEntryCleanup();
  return status;
}
