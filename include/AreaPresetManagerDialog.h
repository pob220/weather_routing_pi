#pragma once

#include <optional>
#include <vector>

#include <wx/dialog.h>

#include "AreaPreset.h"

class wxButton;
class wxListBox;
class wxStaticText;

class AreaPresetManagerDialog : public wxDialog {
public:
  AreaPresetManagerDialog(wxWindow* parent,
                          const std::vector<xgrib::AreaPreset>& presets,
                          const xgrib::AreaPreset& current_bounds,
                          const std::optional<xgrib::AreaPreset>& chart_bounds,
                          const wxString& selected_id = {});

  const std::vector<xgrib::AreaPreset>& Presets() const { return m_presets; }
  wxString SelectedPresetId() const;

private:
  void OnSelectionChanged(wxCommandEvent& event);
  void OnEdit(wxCommandEvent& event);
  void OnNewFromCurrent(wxCommandEvent& event);
  void OnNewFromChart(wxCommandEvent& event);
  void OnDuplicate(wxCommandEvent& event);
  void OnDelete(wxCommandEvent& event);
  void OnMoveUp(wxCommandEvent& event);
  void OnMoveDown(wxCommandEvent& event);
  void OnRestoreSupplied(wxCommandEvent& event);
  void OnSave(wxCommandEvent& event);

  bool EditPreset(xgrib::AreaPreset* preset, int ignored_index);
  void AddFromBounds(const xgrib::AreaPreset& bounds,
                     const wxString& suggested_name);
  void RefreshList(int selection = -1);
  void UpdateSelectionDetails();

  std::vector<xgrib::AreaPreset> m_presets;
  xgrib::AreaPreset m_currentBounds;
  std::optional<xgrib::AreaPreset> m_chartBounds;
  wxListBox* m_list{nullptr};
  wxStaticText* m_details{nullptr};
  wxButton* m_editButton{nullptr};
  wxButton* m_duplicateButton{nullptr};
  wxButton* m_deleteButton{nullptr};
  wxButton* m_upButton{nullptr};
  wxButton* m_downButton{nullptr};
  wxButton* m_chartButton{nullptr};
};
