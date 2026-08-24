#ifndef __SETTINGS_FILTER_EDITOR_H__
#define __SETTINGS_FILTER_EDITOR_H__

// ---------------------------------------------------------------------------
// THE FILTER EDITOR — available fields on the left, the condition TREE on the right.
//
// The platform's only one. A filter is a tree of And/Or/Not groups over conditions,
// and each condition's two sides are VALUES edited through the runtime's own doors
// (a field through the source picker, everything else through its type). Anything
// that lets a person build a filter has to be THIS, not a smaller lookalike — two
// filter editors would be two sets of rules about one object.
//
// It is the second of the two things the list's world and the composer's world
// share (Max, 2026-08-20). Each embeds it where it belongs: a tab in the list's
// settings window, and — for a composition — one per line of its structure, where
// a filter is a property of the level it sits on.
//
// It edits a FILTER DESCRIPTION the host owns — the window's own copy, which it
// puts back on OK. Nothing in here knows about a model, a list or a report.
//
// ⚠ THE PART, NOT THE WHOLE. A composition's filter lives in its settings; a
// LEVEL's filter lives in the level. Both are the same ibFilterDescription, and
// handing this editor that part is what lets one editor serve both without being
// told which world it is in.
// ---------------------------------------------------------------------------

#include <functional>   // std::function — the "it changed" callback a host installs

#include <wx/panel.h>
#include <wx/toolbar.h>
#include <wx/treectrl.h>

#include "backend/compositionDescription.h"   // ibFilterDescription — what is edited here
#include "frontend/win/ctrls/dataview/dataview.h"
#include "frontend/win/dlgs/settings/filterTreeModel.h"   // ibFilterTreeModel + ibFilterPath — a row IS a path

class ibSettingsFieldTree;
class ibMetaData;

class ibFilterEditor : public wxPanel {
public:

	// `filter` — the description whose TREE is edited (never owned here).
	// `fields` — which fields may be picked (never owned here either).
	ibFilterEditor(wxWindow* parent, ibFilterDescription* filter, ibSettingsFieldTree* fields);

	// The filter was replaced or re-read (a variant switch, a fresh load) — start over on it.
	void SetFilter(ibFilterDescription* filter);
	void Reload();

	// The available fields changed (the query was edited) — re-fill the left pane.
	void ReloadFields();

	// ⭐ VIEW ONLY — the conditions stay readable, none of them can be touched. Lives HERE, in the
	// shared editor, so the composer's world and the list's world get one answer rather than two:
	// this is exactly the piece they hold in common (Max, 2026-08-20: "and all of this goes for the
	// dynamic list too"). Shuts both roads to the verbs — the toolbar and the context menu — and the
	// cell editor with them.
	void SetReadOnly(bool readOnly = true);
	bool IsReadOnly() const { return m_readOnly; }

	// ⭐⭐ WHOSE WINDOW THIS IS, and it decides one thing: whether a line marked **Inaccessible** is
	// shown. That display mode means "applied, never shown", so it can only mean anything where
	// there is somebody to hide it FROM — a reader. The designer authors those lines and has to see
	// them, which is why this is a question and not a rule.
	//
	// 🛑 IT MEANT NOTHING AT ALL until 2026-08-24: the mode serialised, was edited in the window and
	// painted back into its own cell, and no code anywhere consulted it. An author could mark a line
	// inaccessible and the reader saw it exactly as before.
	void SetAuthoring(bool authoring);
	bool IsAuthoring() const { return m_authoring; }

	// ⭐ SOMETHING IN HERE WAS CHANGED — told to whoever hosts this editor, the moment it happens
	// (Max, 2026-08-20: "we changed the value, we do not have to press OK — it counts as changed
	// already"). The host decides what that means: in the designer it ends as the configuration
	// being marked modified. Announced HERE rather than at the host's commit, because a commit that
	// announces unconditionally also announces a window that was only opened and closed.
	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }

	// THE FILTER BEING EDITED — the description itself, whose top level is the root
	// group everything hangs under. Null only when no filter has been handed over.
	ibFilterDescription* GetFilter() const { return m_filter; }

	// The cell asks these of its editor — the view it paints in, the model it
	// notifies, the config a value is adjusted by, and who opens the field picker.
	ibDataViewCtrl*    GetFilterView() const { return m_view; }
	ibFilterTreeModel* GetFilterModel() const { return m_model; }
	const ibMetaData*  GetMetaData() const;
	class ibValueCompositionField* ChooseField(wxWindow* parent, const wxString& held) const;

private:

	// The value cell each side of a condition is edited through — a control-backed
	// cell (an ibControlFrame and a type factory), so the runtime's own choice
	// sequence runs here exactly as it does on a form.
	class ibFilterValueRenderer;

	// Re-read the tree after a structural change (added, deleted, moved, grouped)
	// and land the cursor on the line the user just acted on — a command whose
	// result you have to go and find again reads as a command that did nothing.
	// The line is named by its PATH, which is what a row is now.
	void RefreshFilterTree(const ibFilterPath& select = ibFilterPath());
	// Open the filter on its root and every group under it. Runs on the NEXT turn of
	// the event loop (RefreshFilterTree posts it): expanding a row the view has not
	// fetched yet does nothing.
	void ExpandFilterTree();

	// The verbs below are raised by the toolbar and by the context menu alike, so
	// there is one implementation and one set of rules about what is possible where
	// (a group can be added inside a group; only a group can be ungrouped).
	void AddFilterForField(const wxTreeItemId& item);
	void OnFilterAdd(wxCommandEvent&);
	void OnFilterAddGroup(wxCommandEvent&);
	void OnFilterCopy(wxCommandEvent&);
	void OnFilterRemove(wxCommandEvent&);
	void OnFilterGroupSelected(wxCommandEvent&);   // put the chosen lines into a new group
	void OnFilterUngroup(wxCommandEvent&);
	void OnFilterMoveUp(wxCommandEvent&);
	void OnFilterMoveDown(wxCommandEvent&);
	void OnFilterItemActivated(ibDataViewEvent&);
	void OnContextMenu(ibDataViewEvent&);
	// View only, the cell half — a condition refuses to open its editor rather than accepting a
	// change nothing will keep.
	void OnStartEditing(ibDataViewEvent&);

	ibFilterDescription* m_filter = nullptr;
	ibSettingsFieldTree* m_fieldSource = nullptr;

	// THE DESIGNER'S WINDOW BY DEFAULT — see SetAuthoring. An "inaccessible" line has to be visible
	// somewhere, and this is the only place it can be authored.
	bool m_authoring = true;

	ibDataViewCtrl*    m_view      = nullptr;
	ibFilterTreeModel* m_model     = nullptr;
	wxToolBar*         m_toolbar   = nullptr;
	// HELD so a reader's window can hide it — the author's decision about what the reader sees is
	// not a cell the reader is offered. See SetAuthoring.
	class ibDataViewColumn* m_columnDisplayMode = nullptr;
	bool               m_readOnly  = false;   // view only — see SetReadOnly
	// Told on every structural change and every cell edit — see SetOnChanged. Null unless a host
	// asked for it, so an editor nobody wired behaves exactly as it did before.
	std::function<void()> m_onChanged;
	// LOADING, NOT EDITING. Re-reading the buffer (a fresh load, a variant switch) walks the same
	// refresh as a mutation does; without this the editor would announce a change for having been
	// filled in. Set only around Reload, so everything else is by definition somebody's edit.
	bool               m_reloading = false;
	wxTreeCtrl*        m_fieldCtrl = nullptr;
};

#endif // __SETTINGS_FILTER_EDITOR_H__
