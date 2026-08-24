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
// It edits a SORT DESCRIPTION the host owns — the window's own copy, which it puts
// back on OK; whoever embedded it decides when what is here reaches a composer.
// Nothing in here knows about a model, a list or a report.
//
// ⚠ THE PART, NOT THE WHOLE — the same rule the filter editor follows. A
// composition's sort lives in its settings, a LEVEL's sort in the level, and both
// are one ibSortDescription.
// ---------------------------------------------------------------------------

#include <functional>   // std::function — the "it changed" callback a host installs

#include <wx/panel.h>
#include <wx/treectrl.h>

#include "backend/compositionDescription.h"   // ibSortDescription — what is edited here
#include "frontend/win/ctrls/dataview/dataview.h"

class ibSettingsFieldTree;

class ibSortEditor : public wxPanel {
public:

	// `sort`   — the description whose lines are edited (never owned here).
	// `fields` — which fields may be picked (never owned here either; a host may
	//            drive several editors with one).
	ibSortEditor(wxWindow* parent, ibSortDescription* sort, ibSettingsFieldTree* fields);

	// The sort was replaced or re-read (a variant switch, a fresh load) — start over on it.
	void SetSort(ibSortDescription* sort);
	void Reload();

	// The available fields changed (the query was edited) — re-fill the left pane.
	void ReloadFields();

	ibSortDescription* GetSort() const { return m_sort; }

	// ⭐ VIEW ONLY — the twin of ibFilterEditor::SetReadOnly, and shared for the same reason: sort is
	// the OTHER piece the composer's world and the list's world hold in common, so both get one
	// answer. Toolbar disabled, context menu withheld, cell editor refused.
	void SetReadOnly(bool readOnly = true);
	bool IsReadOnly() const { return m_readOnly; }

	// SOMETHING IN HERE WAS CHANGED — the twin of ibFilterEditor::SetOnChanged, same reason and same
	// contract: announced as it happens, not at the host's commit.
	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }

private:

	class ibSortLineModel;   // virtual-list model over the description's sort lines

	// THE ROW INDEX, which is all an edit needs — a sort LINE is a path and a
	// direction, so the index into the description is the whole identity there is.
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

	ibSortDescription*   m_sort   = nullptr;
	ibSettingsFieldTree* m_fieldSource = nullptr;

	ibDataViewCtrl* m_view      = nullptr;
	ibSortLineModel* m_model    = nullptr;
	wxTreeCtrl*     m_fieldCtrl = nullptr;
	class wxToolBar* m_toolbar  = nullptr;   // held so view-only can reach it, as the filter editor's is
	bool            m_readOnly  = false;     // view only — see SetReadOnly
	std::function<void()> m_onChanged;       // told on every change — see SetOnChanged
	bool            m_reloading = false;     // filling from the buffer is not editing — see ibFilterEditor
};

#endif // __SETTINGS_SORT_EDITOR_H__
