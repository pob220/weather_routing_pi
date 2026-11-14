// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlanningTimeCtrl.h"
#include "TimeZoneDisplay.h"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timectrl.h>
#if defined(__OCPN__ANDROID__) && defined(__WXQT__)
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QSignalBlocker>
#endif

wxDEFINE_EVENT(wxEVT_PLANNING_TIME_CHANGED, wxCommandEvent);

PlanningTimeCtrl::PlanningTimeCtrl(wxWindow* parent, const wxDateTime& utc,
    const wxString& name, bool useLocal, const wxString& preferredZone)
    : wxPanel(parent, wxID_ANY) {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  date_ = new wxDatePickerCtrl(this, wxID_ANY, wxDefaultDateTime,
      wxDefaultPosition, wxDefaultSize, wxDP_DROPDOWN | wxDP_SHOWCENTURY);
  clock_ = new wxTimePickerCtrl(this, wxID_ANY);
  date_->SetName(name + _(" date"));
  clock_->SetName(name + _(" clock"));
  row->Add(date_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  row->Add(clock_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  row->Add(new wxStaticText(this, wxID_ANY, _("Time zone")), 0,
           wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  zones_ = new wxChoice(this, wxID_ANY);
  zones_->SetName(name + _(" time zone"));
  zones_->Append("UTC");
  for (const auto& zone : {preferredZone, marine_time::SystemTimeZone()})
    if (marine_time::IsTimeZoneAvailable(zone) && zones_->FindString(zone) == wxNOT_FOUND)
      zones_->Append(zone);
  zones_->SetSelection(0);
  zones_->SetToolTip(_("UTC or the configured/system local time zone. Changing zone preserves a valid instant."));
  row->Add(zones_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  auto* now = new wxButton(this, wxID_ANY, _("Now"));
  now->SetName(name + _(" now"));
  row->Add(now, 0, wxALIGN_CENTER_VERTICAL);
  SetSizer(row);
  if (useLocal && marine_time::IsTimeZoneAvailable(preferredZone)) {
    zone_ = preferredZone;
    zones_->SetStringSelection(zone_);
  }
  SetUtc(utc.IsValid() ? utc : wxDateTime::Now());
  date_->Bind(wxEVT_DATE_CHANGED, [this](wxDateEvent&) { NotifyChanged(); });
  clock_->Bind(wxEVT_TIME_CHANGED, [this](wxDateEvent&) { NotifyChanged(); });
  zones_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    ChangeZone(zones_->GetStringSelection());
    NotifyChanged();
  });
  now->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    SetUtc(wxDateTime::Now());
    NotifyChanged();
  });
}

std::array<int, 6> PlanningTimeCtrl::Fields() const {
  auto date = date_->GetValue(), clock = clock_->GetValue();
#if defined(__OCPN__ANDROID__) && defined(__WXQT__)
  // wxQt's generic pickers can leave their cached values stale after editing.
  // Follow the existing configuration dialog's native/editor fallback.
  const auto value = [](wxWindow* control, bool isDate) {
    auto* native = qobject_cast<QDateTimeEdit*>(control->GetHandle());
    if (!native) native = control->GetHandle()->findChild<QDateTimeEdit*>();
    if (native)
      return wxDateTime(native->date().day(),
          static_cast<wxDateTime::Month>(native->date().month() - 1),
          native->date().year(), native->time().hour(), native->time().minute(),
          native->time().second());
    wxDateTime parsed;
    if (auto* text = control->GetHandle()->findChild<QLineEdit*>()) {
      const auto s = wxString::FromUTF8(text->text().toUtf8().constData());
      if (isDate) parsed.ParseISODate(s); else parsed.ParseISOTime(s);
    }
    return parsed;
  };
  date = value(date_, true);
  clock = value(clock_, false);
#endif
  if (!date.IsValid() || !clock.IsValid()) return {};
  return {date.GetYear(), static_cast<int>(date.GetMonth()) + 1, date.GetDay(),
          clock.GetHour(), clock.GetMinute(), clock.GetSecond()};
}

wxDateTime PlanningTimeCtrl::GetUtc(wxString* error) const {
  if (error) error->clear();
  const auto f = Fields();
  if (f[1] == 0) {
    if (error) *error = _("Choose a valid date and time.");
    return wxInvalidDateTime;
  }
  // Preserve an unedited instant, including the second occurrence of a local
  // autumn clock time and seconds/milliseconds not changed by the user.
  if (savedUtc_.IsValid() && zone_ == savedZone_ && f == savedFields_)
    return savedUtc_;
  const auto resolved = marine_time::FromWallClock(
      f[0], f[1], f[2], f[3], f[4], f[5], zone_);
  if (resolved.status == marine_time::WallClockStatus::Valid) return resolved.utc;
  if (error) {
    if (resolved.status == marine_time::WallClockStatus::Nonexistent)
      *error = _("This local time does not exist because the clocks go forward. Choose another time or select UTC.");
    else if (resolved.status == marine_time::WallClockStatus::Ambiguous)
      *error = _("This local time occurs twice because the clocks go back. Select UTC and enter the intended time, or choose an unambiguous local time.");
    else
      *error = _("Choose a valid date and time.");
  }
  return wxInvalidDateTime;
}

void PlanningTimeCtrl::SetUtc(const wxDateTime& utc) {
  if (!utc.IsValid()) return;
  const auto wall = marine_time::ToWallClock(utc, zone_);
  const auto tz = wxDateTime::UTC;
  // The controls contain civil fields in the host's locale. Use a neutral
  // date for the clock picker so host DST cannot normalise its clock fields.
  wxDateTime date(wall.GetDay(tz), wall.GetMonth(tz), wall.GetYear(tz), 12);
  wxDateTime clock(15, wxDateTime::Jan, 2000, wall.GetHour(tz),
                    wall.GetMinute(tz), wall.GetSecond(tz));
  date_->SetValue(date);
  clock_->SetValue(clock);
#if defined(__OCPN__ANDROID__) && defined(__WXQT__)
  for (auto* control : {static_cast<wxWindow*>(date_), static_cast<wxWindow*>(clock_)})
    if (auto* text = control->GetHandle()->findChild<QLineEdit*>()) {
      const QSignalBlocker blocker(text);
      const auto label = control == date_ ? date.FormatISODate() : clock.FormatISOTime();
      text->setText(QString::fromUtf8(label.utf8_str()));
    }
#endif
  savedUtc_ = utc;
  savedZone_ = zone_;
  savedFields_ = Fields();
}

void PlanningTimeCtrl::ChangeZone(const wxString& requested) {
  const auto utc = GetUtc();
  zone_ = marine_time::IsTimeZoneAvailable(requested) ? requested : wxString("UTC");
  if (zones_->FindString(zone_) == wxNOT_FOUND) zones_->Append(zone_);
  zones_->SetStringSelection(zone_);
  if (utc.IsValid()) SetUtc(utc);
  else savedUtc_ = wxInvalidDateTime; // Keep invalid fields for explicit correction.
}

void PlanningTimeCtrl::SetDisplayTimeZone(bool enabled, const wxString& zone) {
  ChangeZone(enabled ? zone : wxString("UTC"));
}

void PlanningTimeCtrl::NotifyChanged() {
  wxCommandEvent changed(wxEVT_PLANNING_TIME_CHANGED, GetId());
  changed.SetEventObject(this);
  ProcessWindowEvent(changed);
}
