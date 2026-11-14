// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/string.h>

namespace weather_routing {
// Presentation only: retain route identifiers in the model, not in UI labels.
wxString PassageDisplayName(const wxString& name, const wxString& guid,
                           const wxString& start, const wxString& end);
wxString PassageLegLabel(const wxString& name, int leg, int count);
// Shortest decimal text that preserves the stored coordinate on Save.
wxString WaypointCoordinateText(double value);
wxString PassageDirection(const wxString& start, const wxString& end);
}  // namespace weather_routing
