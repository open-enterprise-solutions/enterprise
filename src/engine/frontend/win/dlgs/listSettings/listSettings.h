#ifndef __LIST_SETTINGS_DLG_H__
#define __LIST_SETTINGS_DLG_H__

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/textctrl.h>

#include "backend/composition/listFilter.h"

// Filter tab is now edited THROUGH the runtime (like ibTableViewCtrl::ShowFilter):
// the value column opens a type-driven selector (reference list / number editor /
// enum) instead of a plain text box. Built on the same dataview machinery.
#include "frontend/win/ctrls/dataview/dataview.h"

class BACKEND_API ibValueDynamicList;

// ---------------------------------------------------------------------------
// Visual "Настройка списка" — a modal dialog with three tabs (Отбор /
// Сортировка / Группировка) editing an ibValueListSettings. Registered as
// ibValueListSettings::ms_showDialog at frontend load, so the backend
// (list.EditSettings()) opens it without a frontend dependency.
// First slice of the dynamic-list designer/runtime UI.
// ---------------------------------------------------------------------------
class ibDialogListSettings : public wxDialog {
public:
	// Created ON THE BASIS of a dynamic list: reads the list's source queryable for
	// the available fields and edits the list's settings (GetListSettings()).
	ibDialogListSettings(wxWindow* parent, ibValueDynamicList* list);

	// Opened by the designer property (ibPGDynamicListProperty) against the
	// attribute's dynamic list. OK applies the edits onto the list's composer.
	static bool ShowListSettingsDialog(ibValueDynamicList* list);

	// The renderer reaches back into the dialog for the live filter list and
	// the dataview selection — mirrors wxFilterDialog in tableView.cpp.
	ibValueFilterList* GetFilterList() const;
	ibDataViewCtrl*    GetFilterView() const { return m_filterView; }

private:
	// Forward — the filter model / value renderer live in the .cpp (the
	// reference keeps wxDataViewFilterModel / wxValueViewRenderer local too).
	class ibFilterModel;
	class ibFilterValueRenderer;

	wxWindow* BuildFilterPage(wxWindow* parent);
	wxWindow* BuildOrderPage(wxWindow* parent);
	wxWindow* BuildGroupPage(wxWindow* parent);

	void FillFieldChoice(wxComboBox* choice);
	void LoadFromSettings();
	void ApplyToSettings();

	// Filter row add / remove now mutate the runtime ibValueFilterList directly
	// and refresh the dataview; field/comparison/value are edited INSIDE the row.
	void OnFilterAdd(wxCommandEvent&);
	void OnFilterRemove(wxCommandEvent&);
	void OnFilterItemActivated(ibDataViewEvent&);

	void OnOrderAdd(wxCommandEvent&);
	void OnOrderRemove(wxCommandEvent&);
	void OnGroupAdd(wxCommandEvent&);
	void OnGroupRemove(wxCommandEvent&);
	void OnOk(wxCommandEvent&);

	ibValueDynamicList*  m_list;       // the list this dialog edits (source + settings)
	ibValueListSettings* m_settings;   // = m_list->GetListSettings()

	// Отбор — runtime-driven dataview (Use / Field / Comparison / Value).
	ibDataViewCtrl* m_filterView   = nullptr;
	ibFilterModel*  m_filterModel  = nullptr;
	wxComboBox*     m_filterAddField = nullptr;   // only for "Add" — picks the new row's field
	// Сортировка
	wxListCtrl* m_orderList  = nullptr;
	wxComboBox* m_orderField = nullptr;
	wxChoice*   m_orderDir   = nullptr;
	// Группировка
	wxListCtrl* m_groupList  = nullptr;
	wxComboBox* m_groupField = nullptr;
};

#endif // __LIST_SETTINGS_DLG_H__
