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
	// The filter as the TREE it is — the root group everything hangs under.
	class ibValueFilterGroup* GetFilterRoot() const;
	ibValueSortList*   GetOrderList() const;
	ibValueGroupList*  GetGroupList() const;
	ibDataViewCtrl*    GetFilterView() const { return m_filterView; }
	class ibFilterTreeModel* GetFilterModel() const { return m_filterModel; }

private:
	// Forward — the value renderer lives in the .cpp (kept local there,
	// historically ported from the old filter dialog's renderer). The filter's
	// own model is a TREE and has a file of its own (filterTreeModel.h).
	class ibFilterValueRenderer;
	class ibOrderModel;   // Sort tab dataview model (over the buffer sort list)
	class ibGroupModel;   // Group tab dataview model (over the buffer group list)

	// ---- Page construction — one builder per tab ----
	wxWindow* BuildQueryPage(wxWindow* parent);   // FIRST tab (dynamic-list only) — arbitrary-query source
	wxWindow* BuildFilterPage(wxWindow* parent);
	wxWindow* BuildOrderPage(wxWindow* parent);
	wxWindow* BuildGroupPage(wxWindow* parent);

	// ---- Load / apply / the metadata door ----
	void LoadFromSettings();
	void ApplyToSettings();
	const ibMetaData* SourceMetaData() const;   // config that resolves reference targets (list's, else active)

	// ---- Available-fields tree — the LEFT pane of each tab lists the source's fields (a
	// reference field expands into its target's fields). Shared across Filter / Sort /
	// Group; double-click or Add puts the field into that tab's list on the right. ----
	void PopulateFieldTree(wxTreeCtrl* tree);
	void SelectFieldByPath(wxTreeCtrl* tree, const wxString& path);
	// THE FIELD PICKER — the available-fields tree as a form of its own. A field
	// is a VALUE (CompositionField), so choosing one is choosing a value: the same
	// door a reference opens its selection form through. Returns the chosen field,
	// or null when the user closed the form without picking.
	// `currentPath` — the field the cell already holds, so the picker opens standing
	// on it instead of at the top of the list.
	class ibValueCompositionField* ChooseField(wxWindow* parent, const wxString& currentPath = wxEmptyString);
	friend class ibRowValueCellRenderer;   // the shared row-value cell opens the picker for all three tabs
	void OnFieldTreeExpanding(wxTreeEvent&);
	void OnFieldTreeBeginDrag(wxTreeEvent&);   // drag a field out of a tree -> dropping on the right panel adds it

	// ---- Context menu — one handler for all three tabs, routed by the view that fired ----
	void OnListContextMenu(ibDataViewEvent&);   // right-click a composition list row -> Add/Remove command menu

	// ---- FILTER tab. The verbs below are raised by the toolbar and by the context
	// menu alike, so there is one implementation and one set of rules about what is
	// possible where (a group can be added inside a group; only a group can be
	// ungrouped). The tab is a TREE — its model lives in filterTreeModel.h. ----
	// Re-read the tree after a structural change (added, deleted, moved, grouped)
	// and land the cursor on the line the user just acted on — a command whose
	// result you have to go and find again reads as a command that did nothing.
	void RefreshFilterTree(const ibValue& select = ibValue());
	void AddFilterForField(const wxTreeItemId& item);
	void OnFilterFieldActivated(wxTreeEvent&);
	void OnFilterAdd(wxCommandEvent&);   // add the SELECTED available-field as a filter row
	void OnFilterAddGroup(wxCommandEvent&);
	void OnFilterCopy(wxCommandEvent&);
	void OnFilterRemove(wxCommandEvent&);
	void OnFilterGroupSelected(wxCommandEvent&);     // put the chosen lines into a new group
	void OnFilterUngroup(wxCommandEvent&);
	void OnFilterMoveUp(wxCommandEvent&);
	void OnFilterMoveDown(wxCommandEvent&);
	void OnFilterItemActivated(ibDataViewEvent&);

	// ---- SORT tab. The order IS the setting here, hence the move commands. ----
	class ibValueSortItem* OrderLineAt(const ibDataViewItem& row) const;
	void AddOrderForField(const wxTreeItemId& item);
	void OnOrderFieldActivated(wxTreeEvent&);
	void OnOrderAdd(wxCommandEvent&);
	void OnOrderRemove(wxCommandEvent&);
	void MoveOrderLine(int delta);

	// ---- GROUP tab. Same shape as Sort. ----
	size_t GroupIndexAt(const ibDataViewItem& row) const;
	void AddGroupForField(const wxTreeItemId& item);
	void OnGroupFieldActivated(wxTreeEvent&);
	void OnGroupAdd(wxCommandEvent&);
	void OnGroupRemove(wxCommandEvent&);
	void MoveGroupLine(int delta);

	// ---- Shared by the two flat tabs. A virtual-list row id is 1-based. ----
	static void SelectLastRow(ibDataViewCtrl* view, size_t count);   // cursor follows what was just added

	// ---- Commit ----
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
	class ibFilterTreeModel* m_filterModel = nullptr;   // the TREE (filterTreeModel.h)
	wxToolBar*      m_filterToolbar = nullptr;
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
