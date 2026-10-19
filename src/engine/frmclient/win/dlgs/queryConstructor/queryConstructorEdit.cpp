////////////////////////////////////////////////////////////////////////////
//	Description : The query constructor - the verbs (add / remove / move / rename / edit)
//	              (queryConstructor.h)
////////////////////////////////////////////////////////////////////////////

// THE DESKTOP'S FILE (frontend/win/dlgs/queryConstructor/queryConstructorEdit.cpp), so far its area picker: the
// ticked tree of the groupings a figure is computed over (ibPickGroupingScope). It knows no query and no
// configuration — the rows arrive ready — so it runs on the client as it is and asks the server nothing.
//
// ⚠ NOT HERE YET: the constructor's verbs (ibDialogQueryConstructor::*, PickTotalsScope with them) — they edit the
// query's tree, which is read and written by the engine's parser and renderer (see queryConstructor.h).

#include "queryConstructor.h"

#include "frmclient/backend/compiler/value.h"   // ibValue::GetIconGroup — a row with no picture of its own

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/treectrl.h>      // the area picker's tree — checkboxes, one row per grouping
#include <wx/imaglist.h>      // …and its pictures, taken from the same icons the grids draw
#include <wx/tokenzr.h>       // the cell holds the ticked names comma-separated
#include "frmclient/win/ctrls/checktree.h"   // ibCheckTree — the same ticked tree "Select data type" uses

#include <utility>

namespace {

// ⭐⭐ PICKING THE AREA — AS A TREE, because a separator is a NODE and what hangs on it has to LOOK
// like it hangs on it (the same rule the totals grid follows since the SPLIT arc). A flat list can
// spell `Splitter1.Characteristic`, but it cannot SHOW that the level lives inside that separator,
// and a person choosing an area is choosing a place in the ladder.
//
// The first row is the empty one, and it is not a placeholder: no area IS the ordinary answer — the
// figure folds by the ladder and means one thing on each heading.
// What a row of that tree IS: the name the area answers to. A separator row carries none — see Take.
class ibScopeTreeItem : public wxTreeItemData
{
public:
	explicit ibScopeTreeItem(wxString name) : m_name(std::move(name)) {}
	wxString m_name;
};

// ONE ROW OF THAT TREE, as the constructor hands it over: what it is CALLED on screen, the name the
// query writes, its picture, and whether it is a separator (a node that cannot itself be picked).
struct ibScopeRow {
	wxString caption;
	wxString name;      // empty on a separator — see ibDialogPickTotalsScope::Take
	wxIcon   icon;
	bool     node = false;
	std::vector<ibScopeRow> children;
};

class ibDialogPickTotalsScope : public wxDialog
{
public:
	ibDialogPickTotalsScope(wxWindow* parent, const std::vector<ibScopeRow>& rows, const wxString& current)
		: wxDialog(parent, wxID_ANY, _("Computed over"), wxDefaultPosition, wxSize(360, 420),
		           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	{
		m_tree = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		                        wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT);
		// ⭐ THE SAME PICTURES AS EVERY OTHER LIST IN THIS WINDOW. A field is drawn by its own icon
		// (IconOfExpr over the level's head), a separator by the grouping picture — so the tree reads
		// as the grids beside it and not as a dialog from somewhere else. One image list, the
		// engine's (ibValue::GetIconGroup), because a second copy is how two windows come to disagree
		// about what a reference looks like.
		// ⭐ TICKED, NOT SELECTED. What is said here is "these are the groupings the figure is computed
		// over" — a statement about SEVERAL rows at once, and a tick says that where a highlight
		// cannot: a selection is where the caret happens to be, a tick is a decision.
		//
		// ⚠ THE TICKS ARE THE CONTROL'S OWN — this is ibCheckTree, the very tree "Select data type"
		// uses (Max, 2026-08-27: "take that control as the picker"). Two windows asking the same KIND
		// of question — tick what applies, in a tree — must not look like two different programs, and
		// sharing the control is the only way they cannot drift apart.
		m_images = new wxImageList(16, 16);
		m_tree->AssignImageList(m_images);

		const wxTreeItemId root = m_tree->AddRoot(wxT("/"));
		for (const ibScopeRow& row : rows)
			Add(root, row, current);
		m_tree->ExpandAll();

		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(m_tree, 1, wxEXPAND | wxALL, FromDIP(6));
		sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(6));
		SetSizer(sizer);

		// A double-click ticks the row — the gesture nearest the mouse does the thing the dialog is for.
		m_tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& e) {
			const wxTreeItemId item = e.GetItem();
			if (item.IsOk() && dynamic_cast<ibScopeTreeItem*>(m_tree->GetItemData(item)) != nullptr)
				m_tree->Check(item, !IsTicked(item));
		});
	}

	// ⚠ A SEPARATOR ITSELF IS NOT AN AREA. It does not narrow the rows — it groups the same ones
	// another way — so "the total over a branch" is the total of the node it hangs from. Its row
	// carries no data and therefore cannot be ticked into an answer.
	wxString Ticked() const
	{
		wxString out;
		Collect(m_tree->GetRootItem(), out);
		return out;
	}

private:
	void Collect(const wxTreeItemId& parent, wxString& out) const
	{
		wxTreeItemIdValue cookie;
		for (wxTreeItemId item = m_tree->GetFirstChild(parent, cookie); item.IsOk();
		     item = m_tree->GetNextChild(parent, cookie)) {
			const auto* data = dynamic_cast<ibScopeTreeItem*>(m_tree->GetItemData(item));
			if (data != nullptr && !data->m_name.IsEmpty() && IsTicked(item)) {
				if (!out.IsEmpty()) out += wxT(", ");
				out += data->m_name;
			}
			Collect(item, out);   // …and a separator's own levels, which is where most of them live
		}
	}

	// One row and everything under it. The picture comes from the constructor (IconOfExpr), so the
	// tree draws a field exactly as the grids beside it do.
	void Add(const wxTreeItemId& parent, const ibScopeRow& row, const wxString& current)
	{
		// ⭐ EVERY ROW GETS A PICTURE. The field's own where it resolves, the generic field icon where
		// it does not — a level whose path the model cannot resolve right now (a temp table not yet
		// built, a source being edited) is still a level, and a blank where its neighbours have icons
		// reads as "this one is broken" rather than as "its type is not known this second".
		const wxIcon icon = row.icon.IsOk() ? row.icon : ibValue::GetIconGroup();
		const int image = icon.IsOk() ? m_images->Add(icon) : -1;
		const wxTreeItemId item = m_tree->AppendItem(parent, row.caption, image, image);
		if (!row.node) {
			m_tree->SetItemData(item, new ibScopeTreeItem(row.name));
			// Already named in the cell? Then it comes back ticked — the dialog opens on what IS,
			// not on an empty sheet. A separator gets no state picture at all: it cannot be ticked.
			m_tree->MakeCheckable(item, !row.name.IsEmpty() && IsNamedIn(current, row.name));
		}
		for (const ibScopeRow& child : row.children)
			Add(item, child, current);
	}

	// The cell holds one name or several, comma-separated. Matched whole so `Item` does not tick
	// `ItemGroup` beside it.
	static bool IsNamedIn(const wxString& list, const wxString& name)
	{
		wxStringTokenizer parts(list, wxT(","));
		while (parts.HasMoreTokens()) {
			wxString one = parts.GetNextToken();
			one.Trim(true).Trim(false);
			if (one.IsSameAs(name, false))
				return true;
		}
		return false;
	}

	// State 1 = ticked. A row with no state picture (a separator) reads as not ticked, which is what
	// it is: it cannot be an area at all.
	bool IsTicked(const wxTreeItemId& item) const { return m_tree->GetItemState(item) == ibCheckTree::CHECKED; }

	ibCheckTree* m_tree   = nullptr;
	wxImageList* m_images = nullptr;   // owned by the tree (AssignImageList)
};

} // namespace

// The area, ticked in the tree above. True = the caller's text was replaced.
//
// ⭐ THE ROWS ARE BUILT HERE, where the query and its pictures are known, and handed over ready. The
// dialog then knows nothing about the AST — it shows rows and returns what was ticked, which is all
// a picker is.
// See the declaration: the ticked tree, shared by both windows that ask what a figure is computed
// over. Rows arrive ready — this knows nothing about queries or compositions, which is what lets one
// control serve two tiers.
bool ibPickGroupingScope(wxWindow* parent, const std::vector<wxString>& groupings,
                         const std::vector<wxString>& separators, wxString& inOut)
{
	std::vector<ibScopeRow> rows;
	const auto separatorOf = [&separators](const wxString& name) -> wxString {
		for (const wxString& node : separators)
			if (name.StartsWith(node + wxT(".")))
				return node;
		return wxString();
	};
	for (const wxString& name : groupings) {
		const wxString node = separatorOf(name);
		ibScopeRow row;
		row.name    = name;
		row.caption = node.IsEmpty() ? name : name.Mid(node.length() + 1);
		if (node.IsEmpty()) { rows.push_back(std::move(row)); continue; }

		// …under its separator, which is a NODE: shown, expanded, and not tickable itself.
		ibScopeRow* parentRow = nullptr;
		for (ibScopeRow& had : rows)
			if (had.node && had.caption == node) { parentRow = &had; break; }
		if (parentRow == nullptr) {
			ibScopeRow made;
			made.caption = node;
			made.node    = true;
			rows.push_back(std::move(made));
			parentRow = &rows.back();
		}
		parentRow->children.push_back(std::move(row));
	}

	ibDialogPickTotalsScope dialog(parent, rows, inOut);
	if (dialog.ShowModal() != wxID_OK)
		return false;
	inOut = dialog.Ticked();   // nothing ticked is the way back to "by the ladder"
	return true;
}
