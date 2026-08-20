#ifndef __SETTINGS_SORT_EDITOR_H__
#define __SETTINGS_SORT_EDITOR_H__

// ---------------------------------------------------------------------------
// THE SORT EDITOR — available fields on the left, the ordered list on the right.
//
// One of the two things the dynamic list's world and the composer's world really
// share (Max, 2026-08-20: "two different worlds — a dynamic list has its own
// grouping; what is really shared is filter and sort"). So it is a widget of its
// own here at the root, and each world embeds it where it belongs: a tab in the
// list's settings window, and — for a composition — one per line of its structure,
// because there a sort is a property of the level it sits on.
//
// It edits a BUFFER (an ibValueListSettings the host owns and commits): whoever
// embedded it decides when what is here reaches a composer. Nothing in here knows
// about a model, a list or a report.
// ---------------------------------------------------------------------------

#include <functional>   // std::function — the "it changed" callback a host installs

#include <wx/panel.h>
#include <wx/treectrl.h>

#include "backend/composition/listFilter.h"
#include "frontend/win/ctrls/dataview/dataview.h"

class ibSettingsFieldTree;

class ibSortEditor : public wxPanel {
public:

	// `settings` — the buffer whose Order list is edited (never owned here).
	// `fields`   — which fields may be picked (never owned here either; a host may
	//              drive several editors with one).
	ibSortEditor(wxWindow* parent, ibValueListSettings* settings, ibSettingsFieldTree* fields);

	// The buffer was replaced or re-read (a variant switch, a fresh load) — start over on it.
	void SetSettings(ibValueListSettings* settings);
	void Reload();

	// The available fields changed (the query was edited) — re-fill the left pane.
	void ReloadFields();

	ibValueSortList* GetOrderList() const {
		return m_settings != nullptr ? m_settings->GetOrder() : nullptr;
	}

	// ⭐ VIEW ONLY — the twin of ibFilterEditor::SetReadOnly, and shared for the same reason: sort is
	// the OTHER piece the composer's world and the list's world hold in common, so both get one
	// answer. Toolbar disabled, context menu withheld, cell editor refused.
	void SetReadOnly(bool readOnly = true);
	bool IsReadOnly() const { return m_readOnly; }

	// SOMETHING IN HERE WAS CHANGED — the twin of ibFilterEditor::SetOnChanged, same reason and same
	// contract: announced as it happens, not at the host's commit.
	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }

private:

	class ibOrderModel;   // virtual-list model over the buffer's sort list

	// THE ROW INDEX, which is all an edit needs.
	//
	// (A call handing back the line OBJECT is deliberately absent. On a live list there is none —
	// GetItem mints a transient and returns a raw pointer nobody owns — so every cell that asked
	// leaked one per repaint, and every cell that WROTE through it lost the edit.)
	size_t IndexAt(const ibDataViewItem& row) const;

	void AddForField(const wxTreeItemId& item);
	void OnAdd(wxCommandEvent&);
	void OnRemove(wxCommandEvent&);
	void MoveLine(int delta);
	void OnContextMenu(ibDataViewEvent&);
	// View only, the cell half — a line refuses to open its editor rather than accepting a change
	// nothing will keep.
	void OnStartEditing(ibDataViewEvent&);
	// Re-read the lines and announce the change — where every mutating command ends.
	void RefreshLines();

	ibValueListSettings* m_settings = nullptr;
	ibSettingsFieldTree* m_fields   = nullptr;

	ibDataViewCtrl* m_view      = nullptr;
	ibOrderModel*   m_model     = nullptr;
	wxTreeCtrl*     m_fieldTree = nullptr;
	class wxToolBar* m_toolbar  = nullptr;   // held so view-only can reach it, as the filter editor's is
	bool            m_readOnly  = false;     // view only — see SetReadOnly
	std::function<void()> m_onChanged;       // told on every change — see SetOnChanged
	bool            m_reloading = false;     // filling from the buffer is not editing — see ibFilterEditor
};

#endif // __SETTINGS_SORT_EDITOR_H__
