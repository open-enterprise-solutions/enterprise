#ifndef __LIST_SETTINGS_DLG_H__
#define __LIST_SETTINGS_DLG_H__

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/treectrl.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>

#include "backend/composition/listFilter.h"

// The Filter tab's value column opens a type-driven selector (reference list / number editor / enum) instead of
// a plain text box, built on the dataview machinery below.
#include "frontend/win/ctrls/dataview/dataview.h"

class BACKEND_API ibValueModel;
class BACKEND_API ibValueDynamicList;
class BACKEND_API ibMetaData;

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
	ibValueSortList*   GetOrderList() const;
	ibValueGroupList*  GetGroupList() const;
	ibDataViewCtrl*    GetFilterView() const { return m_filterView; }

private:
	// Forward — the filter model / value renderer live in the .cpp (kept local
	// there, historically ported from the old filter dialog's model/renderer).
	class ibFilterModel;
	class ibFilterValueRenderer;
	class ibOrderModel;   // Sort tab dataview model (over the buffer sort list)
	class ibGroupModel;   // Group tab dataview model (over the buffer group list)

	wxWindow* BuildQueryPage(wxWindow* parent);   // FIRST tab (dynamic-list only) — arbitrary-query source
	wxWindow* BuildFilterPage(wxWindow* parent);
	wxWindow* BuildOrderPage(wxWindow* parent);
	wxWindow* BuildGroupPage(wxWindow* parent);

	void LoadFromSettings();
	void ApplyToSettings();

	// Available-fields tree — the LEFT pane of each tab lists the source's fields (a reference
	// field expands into its target's fields). Shared across Filter / Sort / Group; double-click
	// or Add puts the field into that tab's list on the right.
	const ibMetaData* SourceMetaData() const;   // config that resolves reference targets (list's, else active)
	void PopulateFieldTree(wxTreeCtrl* tree);
	void OnFieldTreeExpanding(wxTreeEvent&);
	void OnFieldTreeBeginDrag(wxTreeEvent&);   // drag a field out of a tree -> dropping on the right panel adds it
	void OnListContextMenu(ibDataViewEvent&);   // right-click a composition list row -> Add/Remove command menu

	void AddFilterForField(const wxTreeItemId& item);
	void OnFilterFieldActivated(wxTreeEvent&);
	void OnFilterAdd(wxCommandEvent&);   // add the SELECTED available-field as a filter row
	void OnFilterRemove(wxCommandEvent&);
	void OnFilterItemActivated(ibDataViewEvent&);

	void AddOrderForField(const wxTreeItemId& item);
	void OnOrderFieldActivated(wxTreeEvent&);
	void OnOrderAdd(wxCommandEvent&);
	void OnOrderRemove(wxCommandEvent&);

	void AddGroupForField(const wxTreeItemId& item);
	void OnGroupFieldActivated(wxTreeEvent&);
	void OnGroupAdd(wxCommandEvent&);
	void OnGroupRemove(wxCommandEvent&);
	void OnOk(wxCommandEvent&);

	// The model this dialog edits. Always set (the dynamic-list ctor passes the list,
	// which IS-A ibValueModel). The field source for the Filter "Add" picker comes
	// from EITHER the model's columns (PATH A) OR the list's source explorer (PATH B).
	ibValueModel*        m_model;      // the model whose composer the dialog commits to on OK
	ibValueDynamicList*  m_list;       // non-null only on the dynamic-list path (source + composer); null for a plain model
	ibValuePtr<ibValueListSettings> m_settings;   // the dialog's OWN transactional BUFFER (load from composer on open, commit on OK)

	// Query tab (dynamic-list only) — arbitrary-query source: enable flag + query text. Edits the list's own
	// UseCustomQuery / CustomQuery properties (not the settings buffer); applied to m_list on OK.
	wxCheckBox* m_queryUseCheck = nullptr;
	wxTextCtrl* m_queryText      = nullptr;

	// Filter — runtime-driven dataview (Use / Field / Comparison / Value).
	ibDataViewCtrl* m_filterView   = nullptr;
	ibFilterModel*  m_filterModel  = nullptr;
	wxTreeCtrl*     m_filterFieldTree = nullptr;   // Filter tab — available fields (left pane), dot-walkable
	wxTreeItemId    m_dragItem;                    // field being dragged from a left tree (drop on the right adds it)
	// Sort — Field + editable Direction, model-driven (like Filter).
	ibDataViewCtrl* m_orderView      = nullptr;
	ibOrderModel*   m_orderModel     = nullptr;
	wxTreeCtrl*     m_orderFieldTree = nullptr;   // Sort tab — available fields (left pane)
	// Group — Field, model-driven.
	ibDataViewCtrl* m_groupView      = nullptr;
	ibGroupModel*   m_groupModel     = nullptr;
	wxTreeCtrl*     m_groupFieldTree = nullptr;   // Group tab — available fields (left pane)
};

#endif // __LIST_SETTINGS_DLG_H__
