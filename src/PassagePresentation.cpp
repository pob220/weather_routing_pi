// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassagePresentation.h"
#include <wx/intl.h>
#include <charconv>
#include <clocale>

namespace weather_routing {
wxString PassageDisplayName(const wxString& name, const wxString& guid,
                           const wxString& start, const wxString& end) {
  wxString label = name;
  label.Trim(true).Trim(false);
  if (!label.empty() && (guid.empty() || !label.IsSameAs(guid, false)))
    return label;
  if (!start.empty() && !end.empty())
    return wxString::Format(_("%s to %s"), start, end);
  return _("Unnamed passage");
}
wxString PassageLegLabel(const wxString& name, int leg, int count) {
  // Wide literals bypass the Windows ANSI code page, including for gettext.
  return name + wxString::Format(_(L" • leg %d/%d"), leg, count);
}
wxString WaypointCoordinateText(double value) {
  char buffer[64];
  const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (converted.ec != std::errc()) return {};
  wxString text = wxString::FromUTF8(buffer, converted.ptr - buffer);
  // ToDouble uses the current numeric locale for the editable fields.
  text.Replace(".", wxString::FromUTF8(std::localeconv()->decimal_point));
  return text;
}
wxString PassageDirection(const wxString& start, const wxString& end) {
  return start + L"  →  " + end;
}
}  // namespace weather_routing
