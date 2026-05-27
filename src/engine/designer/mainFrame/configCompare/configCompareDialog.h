#ifndef __CONFIG_COMPARE_DIALOG_H__
#define __CONFIG_COMPARE_DIALOG_H__

#include "configCompareModel.h"

#include <wx/dialog.h>
#include <wx/aui/auibar.h>

#include <functional>

class ibValueMetaObject;

// ibDialogConfigCompare
//
// Modal dialog showing a side-by-side configuration diff and letting
// the user mark items to merge. The dialog walks the diff at
// construction time via ibMetaDiffWalker, drops the result into
// ibDataViewMetaDiffModel, and renders it in an ibDataViewCtrl with
// a checkbox column.
//
// Phase C scope (this commit):
//   - Toolbar: Select all differences / Clear / Expand all / Collapse all
//   - Main pane: ibDataViewCtrl with checkbox + name + status + left/right
//   - Bottom: Apply Merge (disabled until Phase E lands) + Close
//
// Phase D adds the source-picker entry point that constructs this
// dialog from designer menu. Phase E wires the Apply Merge button to
// CopyObject-based transfer.

class ibDialogConfigCompare : public wxDialog {
public:

	// Construct with two metadata roots. Either may be nullptr — the
	// walker tolerates one-sided diffs (everything becomes OnlyInLeft
	// or OnlyInRight). The labels appear in the column headers and
	// status text so the user knows which side is which (e.g.
	// "Current" vs "Other file.xml").
	ibDialogConfigCompare(
		wxWindow* parent,
		ibValueMetaObject* leftRoot,
		ibValueMetaObject* rightRoot,
		const wxString& leftLabel,
		const wxString& rightLabel
	);

	virtual ~ibDialogConfigCompare() = default;

	// Save callback for the right-side config — called after a Push
	// merge so the file (or other persistent backing of the right
	// configuration) reflects the mutations. When unset, Push direction
	// is hidden from the direction switch since there's nowhere to
	// persist the result.
	void SetRightSaveCallback(std::function<bool()> cb) {
		m_rightSaveCallback = std::move(cb);
		RefreshDirectionChoice();
	}

	// Re-populate the direction wxChoice based on whether the right
	// side has a save target. Called by SetRightSaveCallback so the
	// user only sees Push when it makes sense.
	void RefreshDirectionChoice();

private:

	enum {
		wxID_TOOL_SELECT_ALL = wxID_HIGHEST + 1,
		wxID_TOOL_CLEAR,
		wxID_TOOL_EXPAND_ALL,
		wxID_TOOL_COLLAPSE_ALL,
	};

	void BuildUI(const wxString& leftLabel, const wxString& rightLabel);

	void OnSelectAll(wxCommandEvent& event);
	void OnClearSelection(wxCommandEvent& event);
	void OnExpandAll(wxCommandEvent& event);
	void OnCollapseAll(wxCommandEvent& event);
	void OnApplyMerge(wxCommandEvent& event);
	void OnFilterChanged(wxCommandEvent& event);
	void OnDirectionChanged(wxCommandEvent& event);

	// Merge primitives — direction-parametrized.
	//   pull = true  → source=right, target=left  (other → current)
	//   pull = false → source=left,  target=right (current → other)
	void ApplyAdd(const struct ibMetaDiffRecord& rec, bool pull);
	void ApplyDelete(const struct ibMetaDiffRecord& rec, bool pull);
	void ApplyReplace(const struct ibMetaDiffRecord& rec, bool pull);

	// True when any ancestor of the record has OnlyInLeft / OnlyInRight
	// status — that ancestor's create/delete already handles this
	// descendant via PasteObject's recursion, so we skip to avoid
	// double-application.
	bool HasOnlyInAncestor(int recordIndex) const;

	// Walk ancestors to find the target-side parent for a record.
	// Returns nullptr when no paired ancestor exists on the target
	// side (shouldn't happen for valid diff inputs — the root pair is
	// always paired).
	class ibValueMetaObject* FindTargetParent(
		const struct ibMetaDiffRecord& rec, bool pull) const;

	// Veto handler — the root config row is the user's entry point into
	// the diff. Collapsing it would hide everything; we treat it as a
	// permanent fixture of the dialog.
	void OnItemCollapsing(class ibDataViewEvent& event);

	void RefreshApplyButtonState();

	// ibDataViewCtrl has no ExpandAll / CollapseAll — walk the model and
	// flip each container item one by one. The model returns children
	// via GetChildren, so recursion mirrors the on-screen tree shape.
	void ExpandAllRecursive(const ibDataViewItem& parent);
	void CollapseAllRecursive(const ibDataViewItem& parent);

	wxAuiToolBar* m_toolbar = nullptr;
	ibDataViewCtrl* m_dataView = nullptr;
	class wxChoice* m_filterChoice = nullptr;
	class wxChoice* m_directionChoice = nullptr;
	wxButton* m_buttonApply = nullptr;
	wxButton* m_buttonClose = nullptr;

	std::function<bool()> m_rightSaveCallback;

	// Labels are remembered so RefreshDirectionChoice can rebuild the
	// dropdown after SetRightSaveCallback runs (the labels appear in
	// the "left -> right" arrow-style entries).
	wxString m_directionLeftLabel;
	wxString m_directionRightLabel;

	// One-shot latch — flips to true after the first wxEVT_SHOW so
	// the root-expand handler doesn't re-fire on subsequent
	// show/hide cycles (e.g. when the dialog is hidden behind a
	// modal child).
	bool m_rootInitiallyExpanded = false;

	// wxObjectDataPtr so the dataview ctrl owns the model lifetime —
	// matches how predefinedEditor binds its tree store.
	wxObjectDataPtr<ibDataViewMetaDiffModel> m_model;
};

#endif // __CONFIG_COMPARE_DIALOG_H__
