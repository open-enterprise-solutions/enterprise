#ifndef __LIST_SETTINGS_DLG_H__
#define __LIST_SETTINGS_DLG_H__

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/textctrl.h>

#include "backend/composition/listFilter.h"

// The Filter tab's value column opens a type-driven selector (reference list / number editor / enum) instead of
// a plain text box, built on the dataview machinery below.
#include "frontend/win/ctrls/dataview/dataview.h"

class BACKEND_API ibValueModel;
class BACKEND_API ibValueDynamicList;

// ---------------------------------------------------------------------------
// Visual "List settings" — a modal dialog with three tabs (Filter /
// Sort / Group) editing an ibValueListSettings. Opened through the static
// ShowListSettingsDialog(...) entry (overloaded for a dynamic list or any
// model), so callers reach it without holding the dialog class directly.
// First slice of the dynamic-list designer/runtime UI.
// ---------------------------------------------------------------------------
class ibDialogListSettings : public wxDialog {
public:
	// Created ON THE BASIS of a dynamic list: reads the list's source queryable for
	// the available fields and edits the list's settings (GetListSettings()).
	ibDialogListSettings(wxWindow* parent, ibValueDynamicList* list);

	// Created ON THE BASIS of ANY model: edits the model's settings
	// (GetListSettings()) and builds the available filter fields from the model's
	// COLUMNS (PATH A — see BuildFilterFieldsFromColumns). Used when the "Filter"
	// button is pressed on a plain table model (no dynamic-list source yet).
	ibDialogListSettings(wxWindow* parent, ibValueModel* model);

	// Opened by the designer property (ibPGDynamicListProperty) against the
	// attribute's dynamic list. OK applies the edits onto the list's composer.
	static bool ShowListSettingsDialog(ibValueDynamicList* list);

	// General entry: open the settings window for ANY model. The filter fields come
	// from the model's columns (PATH A). OK commits the dialog's buffer onto the
	// model's composer (ibCommitSettingsToComposer + NotifyReset).
	static bool ShowListSettingsDialog(ibValueModel* model);

	// The renderer reaches back into the dialog for the buffer filter list and
	// the dataview selection (historically ported from the old filter dialog).
	ibValueFilterList* GetFilterList() const;
	ibDataViewCtrl*    GetFilterView() const { return m_filterView; }

private:
	// Forward — the filter model / value renderer live in the .cpp (kept local
	// there, historically ported from the old filter dialog's model/renderer).
	class ibFilterModel;
	class ibFilterValueRenderer;

	// One selectable filter field: its technical name / dot-path (what the composer
	// dot-walks) plus the leaf's id/type (so the Value column edits through the
	// runtime). Built by BuildFilterFields() — from the model's columns (PATH A) or
	// the list's source explorer (PATH B).
	struct ibFilterFieldInfo {
		wxString          m_path;
		ibMetaID          m_leafId = wxNOT_FOUND;
		ibTypeDescription m_type;
	};

	wxWindow* BuildFilterPage(wxWindow* parent);
	wxWindow* BuildOrderPage(wxWindow* parent);
	wxWindow* BuildGroupPage(wxWindow* parent);

	void FillFieldChoice(wxComboBox* choice);

	// =====================================================================
	//  FIELD SOURCE — the single replaceable point for "what can be filtered".
	//
	//  BuildFilterFields() dispatches to ONE field-source builder:
	//    * PATH A (current): BuildFilterFieldsFromColumns() — the MODEL'S COLUMNS.
	//    * PATH B (future, dynamic list): BuildFilterFieldsFromSource() — the
	//      source explorer (root columns + one hop into each reference).
	//
	//  To swap the general (model) path onto a queryable-based field source later,
	//  change exactly ONE function (BuildFilterFieldsFromColumns) — nothing else in
	//  the dialog reads the field source directly; it only consumes m_filterFields.
	// =====================================================================
	void BuildFilterFields();

	// PATH A — fill m_filterFields from the model's ibValueModelColumnCollection:
	// one ibFilterFieldInfo per column { m_path = column name, m_leafId = column id,
	// m_type = column type }. This is the SINGLE function to replace when switching
	// the field source to a queryable (PATH B) — see BuildFilterFields() above.
	void BuildFilterFieldsFromColumns();

	// PATH B — fill m_filterFields from the dynamic list's source explorer: every
	// root column PLUS one hop into each reference column (Supplier.Region). The
	// stored path is a dot-walk technical name — ibDataDBComposer::Filter dot-walks it
	// (auto-JOIN). TODO(unify): a full recursive tree picker like the one in
	// advpropSource — collapse the two into one shared source-field picker.
	void BuildFilterFieldsFromSource();
	void LoadFromSettings();
	void ApplyToSettings();

	// Filter row add / remove mutate the dialog's BUFFER ibValueFilterList
	// directly and refresh the dataview; field/comparison/value are edited
	// INSIDE the row (the buffer is committed to the composer on OK).
	void OnFilterAdd(wxCommandEvent&);
	void OnFilterRemove(wxCommandEvent&);
	void OnFilterItemActivated(ibDataViewEvent&);

	void OnOrderAdd(wxCommandEvent&);
	void OnOrderRemove(wxCommandEvent&);
	void OnGroupAdd(wxCommandEvent&);
	void OnGroupRemove(wxCommandEvent&);
	void OnOk(wxCommandEvent&);

	// The model this dialog edits. Always set (the dynamic-list ctor passes the list,
	// which IS-A ibValueModel). The field source for the Filter "Add" picker comes
	// from EITHER the model's columns (PATH A) OR the list's source explorer (PATH B).
	ibValueModel*        m_model;      // the model whose composer the dialog commits to on OK
	ibValueDynamicList*  m_list;       // non-null only on the dynamic-list path (source + composer); null for a plain model
	ibValuePtr<ibValueListSettings> m_settings;   // the dialog's OWN transactional BUFFER (load from composer on open, commit on OK)

	// Filter — runtime-driven dataview (Use / Field / Comparison / Value).
	ibDataViewCtrl* m_filterView   = nullptr;
	ibFilterModel*  m_filterModel  = nullptr;
	wxComboBox*     m_filterAddField = nullptr;   // only for "Add" — picks the new row's field
	std::vector<ibFilterFieldInfo> m_filterFields;   // BuildFilterFields() output — dot-path fields for the "Add" picker
	// Sort
	wxListCtrl* m_orderList  = nullptr;
	wxComboBox* m_orderField = nullptr;
	wxChoice*   m_orderDir   = nullptr;
	// Group
	wxListCtrl* m_groupList  = nullptr;
	wxComboBox* m_groupField = nullptr;
};

#endif // __LIST_SETTINGS_DLG_H__
