#include "AreaPresetManagerDialog.h"

#include <algorithm>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {

wxString CoordinateText(double value) {
  wxString text = wxString::Format("%.6f", value);
  while (text.Contains(".") && text.EndsWith("0")) text.RemoveLast();
  if (text.EndsWith(".")) text.RemoveLast();
  return text;
}

class AreaPresetDetailsDialog : public wxDialog {
public:
  AreaPresetDetailsDialog(wxWindow* parent, const xgrib::AreaPreset& preset)
      : wxDialog(parent, wxID_ANY, "Area preset", wxDefaultPosition,
                 wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_preset(preset) {
    auto* top = new wxBoxSizer(wxVERTICAL);
    auto* grid = new wxFlexGridSizer(2, 8, 8);
    grid->AddGrowableCol(1, 1);
    m_name = AddField(grid, "Name", preset.name);
    m_west = AddField(grid, "West longitude", CoordinateText(preset.west));
    m_south = AddField(grid, "South latitude", CoordinateText(preset.south));
    m_east = AddField(grid, "East longitude", CoordinateText(preset.east));
    m_north = AddField(grid, "North latitude", CoordinateText(preset.north));
    grid->AddSpacer(1);
    m_useProviderPreferences = new wxCheckBox(
        this, wxID_ANY, "Select preferred providers when this area is applied");
    m_useProviderPreferences->SetValue(!preset.weather_provider.empty() ||
                                       !preset.current_provider.empty());
    grid->Add(m_useProviderPreferences, 1, wxEXPAND);
    m_weatherProvider = AddProviderChoice(grid, "Preferred weather provider",
                                          xgrib::WeatherProviderOptions(),
                                          preset.weather_provider);
    m_currentProvider = AddProviderChoice(grid, "Preferred current provider",
                                          xgrib::CurrentProviderOptions(),
                                          preset.current_provider);
    top->Add(grid, 1, wxEXPAND | wxALL, 12);

    auto* buttons = new wxStdDialogButtonSizer();
    auto* save = new wxButton(this, wxID_OK, "Save");
    buttons->AddButton(save);
    buttons->AddButton(new wxButton(this, wxID_CANCEL, "Cancel"));
    buttons->Realize();
    top->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    SetSizerAndFit(top);
    SetMinSize(wxSize(460, GetSize().GetHeight()));
    CentreOnParent();
    save->Bind(wxEVT_BUTTON, &AreaPresetDetailsDialog::OnSave, this);
    m_useProviderPreferences->Bind(
        wxEVT_CHECKBOX, &AreaPresetDetailsDialog::OnProviderOptionChanged,
        this);
    UpdateProviderControls();
    m_name->SetFocus();
    m_name->SelectAll();
  }

  const xgrib::AreaPreset& Preset() const { return m_preset; }

private:
  wxTextCtrl* AddField(wxFlexGridSizer* grid, const wxString& label,
                       const wxString& value) {
    grid->Add(new wxStaticText(this, wxID_ANY, label), 0,
              wxALIGN_CENTER_VERTICAL);
    auto* field = new wxTextCtrl(this, wxID_ANY, value);
    grid->Add(field, 1, wxEXPAND);
    return field;
  }

  wxChoice* AddProviderChoice(
      wxFlexGridSizer* grid, const wxString& label,
      const std::vector<xgrib::ProviderPreferenceOption>& options,
      const wxString& selected_id) {
    grid->Add(new wxStaticText(this, wxID_ANY, label), 0,
              wxALIGN_CENTER_VERTICAL);
    auto* choice = new wxChoice(this, wxID_ANY);
    choice->Append("Keep current selection");
    int selection = 0;
    for (size_t index = 0; index < options.size(); ++index) {
      choice->Append(options[index].label);
      if (options[index].id == selected_id)
        selection = static_cast<int>(index) + 1;
    }
    choice->SetSelection(selection);
    grid->Add(choice, 1, wxEXPAND);
    return choice;
  }

  wxString SelectedProviderId(
      const wxChoice* choice,
      const std::vector<xgrib::ProviderPreferenceOption>& options) const {
    const int selection = choice->GetSelection();
    if (selection <= 0 || selection > static_cast<int>(options.size()))
      return {};
    return options[static_cast<size_t>(selection - 1)].id;
  }

  void OnProviderOptionChanged(wxCommandEvent&) { UpdateProviderControls(); }

  void UpdateProviderControls() {
    const bool enabled = m_useProviderPreferences->GetValue();
    m_weatherProvider->Enable(enabled);
    m_currentProvider->Enable(enabled);
  }

  void OnSave(wxCommandEvent&) {
    xgrib::AreaPreset candidate = m_preset;
    candidate.name = m_name->GetValue();
    candidate.name.Trim(true);
    candidate.name.Trim(false);
    if (!m_west->GetValue().ToDouble(&candidate.west) ||
        !m_south->GetValue().ToDouble(&candidate.south) ||
        !m_east->GetValue().ToDouble(&candidate.east) ||
        !m_north->GetValue().ToDouble(&candidate.north)) {
      wxMessageBox("Enter a valid number for every coordinate.",
                   "Invalid area preset", wxOK | wxICON_WARNING, this);
      return;
    }
    const wxString error = xgrib::ValidateAreaPreset(candidate);
    if (!error.empty()) {
      wxMessageBox(error, "Invalid area preset", wxOK | wxICON_WARNING, this);
      return;
    }
    if (m_useProviderPreferences->GetValue()) {
      candidate.weather_provider = SelectedProviderId(
          m_weatherProvider, xgrib::WeatherProviderOptions());
      candidate.current_provider = SelectedProviderId(
          m_currentProvider, xgrib::CurrentProviderOptions());
    } else {
      candidate.weather_provider.clear();
      candidate.current_provider.clear();
    }
    m_preset = candidate;
    EndModal(wxID_OK);
  }

  xgrib::AreaPreset m_preset;
  wxTextCtrl* m_name{nullptr};
  wxTextCtrl* m_west{nullptr};
  wxTextCtrl* m_south{nullptr};
  wxTextCtrl* m_east{nullptr};
  wxTextCtrl* m_north{nullptr};
  wxCheckBox* m_useProviderPreferences{nullptr};
  wxChoice* m_weatherProvider{nullptr};
  wxChoice* m_currentProvider{nullptr};
};

}  // namespace

AreaPresetManagerDialog::AreaPresetManagerDialog(
    wxWindow* parent, const std::vector<xgrib::AreaPreset>& presets,
    const xgrib::AreaPreset& current_bounds,
    const std::optional<xgrib::AreaPreset>& chart_bounds,
    const wxString& selected_id)
    : wxDialog(parent, wxID_ANY, "Manage GRIB areas", wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_presets(presets),
      m_currentBounds(current_bounds),
      m_chartBounds(chart_bounds) {
  auto* top = new wxBoxSizer(wxVERTICAL);
  auto* introduction = new wxStaticText(
      this, wxID_ANY,
      "Create named bounding boxes for the regions you use. Each area can "
      "optionally select initial weather and current providers; you can still "
      "change them before generation.");
  introduction->Wrap(570);
  top->Add(introduction, 0, wxEXPAND | wxALL, 12);

  auto* body = new wxBoxSizer(wxHORIZONTAL);
  m_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(330, 260));
  body->Add(m_list, 1, wxEXPAND | wxRIGHT, 10);
  auto* actions = new wxBoxSizer(wxVERTICAL);
  auto addAction = [&](const wxString& label, wxButton** member = nullptr) {
    auto* button = new wxButton(this, wxID_ANY, label);
    actions->Add(button, 0, wxEXPAND | wxBOTTOM, 6);
    if (member) *member = button;
    return button;
  };
  auto* currentButton = addAction("New from current bbox...");
  m_chartButton = addAction("New from chart area...");
  actions->AddSpacer(6);
  addAction("Edit...", &m_editButton);
  addAction("Duplicate...", &m_duplicateButton);
  addAction("Delete", &m_deleteButton);
  actions->AddSpacer(6);
  addAction("Move up", &m_upButton);
  addAction("Move down", &m_downButton);
  body->Add(actions, 0, wxEXPAND);
  top->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

  m_details = new wxStaticText(this, wxID_ANY, "");
  top->Add(m_details, 0, wxEXPAND | wxALL, 12);

  auto* bottom = new wxBoxSizer(wxHORIZONTAL);
  auto* restore = new wxButton(this, wxID_ANY, "Restore supplied areas");
  bottom->Add(restore, 0);
  bottom->AddStretchSpacer(1);
  auto* save = new wxButton(this, wxID_OK, "Save");
  auto* cancel = new wxButton(this, wxID_CANCEL, "Cancel");
  bottom->Add(save, 0, wxRIGHT, 8);
  bottom->Add(cancel, 0);
  top->Add(bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  SetSizerAndFit(top);
  SetMinSize(wxSize(620, 480));
  CentreOnParent();

  m_list->Bind(wxEVT_LISTBOX, &AreaPresetManagerDialog::OnSelectionChanged,
               this);
  m_list->Bind(wxEVT_LISTBOX_DCLICK, &AreaPresetManagerDialog::OnEdit, this);
  currentButton->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnNewFromCurrent,
                      this);
  m_chartButton->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnNewFromChart,
                      this);
  m_editButton->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnEdit, this);
  m_duplicateButton->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnDuplicate,
                          this);
  m_deleteButton->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnDelete, this);
  m_upButton->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnMoveUp, this);
  m_downButton->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnMoveDown, this);
  restore->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnRestoreSupplied,
                this);
  save->Bind(wxEVT_BUTTON, &AreaPresetManagerDialog::OnSave, this);
  m_chartButton->Enable(m_chartBounds.has_value());
  int selection = -1;
  for (size_t index = 0; index < m_presets.size(); ++index) {
    if (m_presets[index].id == selected_id) {
      selection = static_cast<int>(index);
      break;
    }
  }
  RefreshList(selection);
}

wxString AreaPresetManagerDialog::SelectedPresetId() const {
  const int selection = m_list->GetSelection();
  if (selection == wxNOT_FOUND ||
      selection >= static_cast<int>(m_presets.size()))
    return {};
  return m_presets[static_cast<size_t>(selection)].id;
}

void AreaPresetManagerDialog::OnSelectionChanged(wxCommandEvent&) {
  UpdateSelectionDetails();
}

bool AreaPresetManagerDialog::EditPreset(xgrib::AreaPreset* preset,
                                         int ignored_index) {
  if (!preset) return false;
  xgrib::AreaPreset edited = *preset;
  for (;;) {
    AreaPresetDetailsDialog dialog(this, edited);
    if (dialog.ShowModal() != wxID_OK) return false;
    edited = dialog.Preset();
    std::vector<xgrib::AreaPreset> proposed = m_presets;
    if (ignored_index >= 0 && ignored_index < static_cast<int>(proposed.size()))
      proposed[static_cast<size_t>(ignored_index)] = edited;
    else
      proposed.push_back(edited);
    const wxString error = xgrib::ValidateAreaPresetCollection(proposed);
    if (error.empty()) {
      *preset = edited;
      return true;
    }
    wxMessageBox(error, "Invalid area preset", wxOK | wxICON_WARNING, this);
  }
}

void AreaPresetManagerDialog::AddFromBounds(const xgrib::AreaPreset& bounds,
                                            const wxString& suggested_name) {
  xgrib::AreaPreset added = bounds;
  added.id = xgrib::NextAreaPresetId(m_presets);
  added.name = xgrib::UniqueAreaPresetName(m_presets, suggested_name);
  if (!EditPreset(&added, -1)) return;
  m_presets.push_back(added);
  RefreshList(static_cast<int>(m_presets.size()) - 1);
}

void AreaPresetManagerDialog::OnNewFromCurrent(wxCommandEvent&) {
  AddFromBounds(m_currentBounds, "New area");
}

void AreaPresetManagerDialog::OnNewFromChart(wxCommandEvent&) {
  if (m_chartBounds) AddFromBounds(*m_chartBounds, "Chart area");
}

void AreaPresetManagerDialog::OnEdit(wxCommandEvent&) {
  const int selection = m_list->GetSelection();
  if (selection == wxNOT_FOUND) return;
  auto edited = m_presets[static_cast<size_t>(selection)];
  if (!EditPreset(&edited, selection)) return;
  m_presets[static_cast<size_t>(selection)] = edited;
  RefreshList(selection);
}

void AreaPresetManagerDialog::OnDuplicate(wxCommandEvent&) {
  const int selection = m_list->GetSelection();
  if (selection == wxNOT_FOUND) return;
  xgrib::AreaPreset duplicate = m_presets[static_cast<size_t>(selection)];
  duplicate.id = xgrib::NextAreaPresetId(m_presets);
  duplicate.name =
      xgrib::UniqueAreaPresetName(m_presets, duplicate.name + " copy");
  if (!EditPreset(&duplicate, -1)) return;
  m_presets.insert(m_presets.begin() + selection + 1, duplicate);
  RefreshList(selection + 1);
}

void AreaPresetManagerDialog::OnDelete(wxCommandEvent&) {
  const int selection = m_list->GetSelection();
  if (selection == wxNOT_FOUND) return;
  const wxString name = m_presets[static_cast<size_t>(selection)].name;
  if (wxMessageBox("Delete the area preset '" + name + "'?",
                   "Delete area preset",
                   wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES)
    return;
  m_presets.erase(m_presets.begin() + selection);
  RefreshList(std::min(selection, static_cast<int>(m_presets.size()) - 1));
}

void AreaPresetManagerDialog::OnMoveUp(wxCommandEvent&) {
  const int selection = m_list->GetSelection();
  if (selection <= 0) return;
  std::swap(m_presets[static_cast<size_t>(selection)],
            m_presets[static_cast<size_t>(selection - 1)]);
  RefreshList(selection - 1);
}

void AreaPresetManagerDialog::OnMoveDown(wxCommandEvent&) {
  const int selection = m_list->GetSelection();
  if (selection == wxNOT_FOUND ||
      selection + 1 >= static_cast<int>(m_presets.size()))
    return;
  std::swap(m_presets[static_cast<size_t>(selection)],
            m_presets[static_cast<size_t>(selection + 1)]);
  RefreshList(selection + 1);
}

void AreaPresetManagerDialog::OnRestoreSupplied(wxCommandEvent&) {
  if (wxMessageBox(
          "Restore the supplied areas to their original names and bounds? "
          "Your own areas will be kept.",
          "Restore supplied areas", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION,
          this) != wxYES)
    return;
  xgrib::RestoreSuppliedAreaPresets(&m_presets);
  RefreshList(m_presets.empty() ? -1 : 0);
}

void AreaPresetManagerDialog::OnSave(wxCommandEvent&) {
  const wxString error = xgrib::ValidateAreaPresetCollection(m_presets);
  if (!error.empty()) {
    wxMessageBox(error, "Invalid area presets", wxOK | wxICON_WARNING, this);
    return;
  }
  EndModal(wxID_OK);
}

void AreaPresetManagerDialog::RefreshList(int selection) {
  m_list->Freeze();
  m_list->Clear();
  for (const auto& preset : m_presets) m_list->Append(preset.name);
  if (!m_presets.empty()) {
    if (selection < 0) selection = 0;
    selection = std::min(selection, static_cast<int>(m_presets.size()) - 1);
    m_list->SetSelection(selection);
  }
  m_list->Thaw();
  UpdateSelectionDetails();
}

void AreaPresetManagerDialog::UpdateSelectionDetails() {
  const int selection = m_list->GetSelection();
  const bool selected = selection != wxNOT_FOUND;
  m_editButton->Enable(selected);
  m_duplicateButton->Enable(selected);
  m_deleteButton->Enable(selected);
  m_upButton->Enable(selected && selection > 0);
  m_downButton->Enable(selected &&
                       selection + 1 < static_cast<int>(m_presets.size()));
  if (!selected) {
    m_details->SetLabel(
        "No saved areas. Use one of the New buttons to add one.");
    return;
  }
  const auto& preset = m_presets[static_cast<size_t>(selection)];
  m_details->SetLabel("Bounds: west " + CoordinateText(preset.west) +
                      ", south " + CoordinateText(preset.south) + ", east " +
                      CoordinateText(preset.east) + ", north " +
                      CoordinateText(preset.north));
}
