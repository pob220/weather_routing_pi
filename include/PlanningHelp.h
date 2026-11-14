// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/button.h>
#include <wx/gdicmn.h>
#include <wx/sizer.h>
#include <vector>

namespace weather_routing {
enum class PlanningHelpTopic {
  Search,
  Safety,
  Shoreline,
  Resources,
  Timing,
  Performance,
  Weather,
  Navigation
};
wxString PlanningHelpTitle(PlanningHelpTopic topic);
wxString PlanningHelpText(PlanningHelpTopic topic);
void ShowPlanningHelp(wxWindow* parent, PlanningHelpTopic topic);
wxButton* AddPlanningHelpButton(wxWindow* parent, wxSizer* sizer,
                                PlanningHelpTopic topic);
void SetPlanningHelp(wxWindow* control, PlanningHelpTopic topic,
                     const wxString& hint);
void ShowFocusedPlanningHelp(wxWindow* parent, PlanningHelpTopic fallback);
// Pure geometry policy: preserve valid positions, including negative monitor
// coordinates, and keep the complete dialog on an available work area.
wxRect VisiblePlanningDialog(wxRect requested, wxSize minimum,
                             const std::vector<wxRect>& workAreas);
void KeepPlanningDialogVisible(wxWindow* window);
}  // namespace weather_routing
