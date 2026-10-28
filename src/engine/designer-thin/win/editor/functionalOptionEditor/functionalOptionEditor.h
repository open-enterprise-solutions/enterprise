#ifndef __FUNCTIONAL_OPTION_EDITOR_H__
#define __FUNCTIONAL_OPTION_EDITOR_H__

// THE MEMBERS OF A FUNCTIONAL OPTION — what is not shown while it is off: the desktop's
// (designer/win/editor/functionalOptionEditor) over the server's rows, as the tab's view writes them (frmserver
// ibFunctionalOptionMembersView, ViewKind FunctionalOption). Same shape as the common attribute's editor next door
// (commonAttributeEditor.h), one step further: the fields and tables inside an object that may belong come under it,
// at every depth — a field is hidden on its own, not only with its object.
//
// Checking a box writes the membership INTO that object (SetFunctionalOption) on the server; the tree is drawn back.

#include <wx/treectrl.h>

#include <set>
#include <vector>

#include "frmclient/docView/tabDocument.h"
#include "frmclient/win/ctrls/checktree.h"

class ibFunctionalOptionEditor : public wxWindow {

	wxTreeItemId m_treeMETADATA;

	// The tab — what a tick is posted to.
	const ibTabDocument* m_document;

	class ibTreeItemObject : public wxTreeItemData {
		long long m_metaId; // the row's object — its NodeId
	public:
		ibTreeItemObject(long long metaId) : m_metaId(metaId) {}
		long long GetMetaId() const { return m_metaId; }
	};

	ibCheckTree* m_membersCtrl;

	long long m_keepSelId = 0;
	wxTreeItemId m_keepSelNode;
	long long m_keepTopId = 0;   // the row at the top — a tick is drawn back by a rebuild, the list stays where it was
	wxTreeItemId m_keepTopNode;
	std::set<long long> m_keepExpandedIds;   // …and the objects opened — a field ticked stays in sight

protected:

	void OnCheckItem(wxTreeEvent& event);

private:

	// A metatype's group row, as the server wrote it — the same caption the designer tree reads.
	wxTreeItemId GroupFor(const ibProtocolNode& row);

	wxTreeItemId AppendItem(const wxTreeItemId& parent, const ibProtocolNode& row);

	// The fields and tables inside an object that may belong on their own — at every depth, so a column of a
	// tabular section can be ticked without its table.
	void AppendChildren(const wxTreeItemId& parent, const ibProtocolNode& row);

	void InitMembers();
	void ClearMembers();
	void FillData(const ibProtocolNode& view);

	// The groups — opened after a fill, the objects under them left closed.
	std::vector<wxTreeItemId> m_groups;

public:

	void RefreshMembers(const ibProtocolNode& view);

	void SetReadOnly(bool readOnly = true) {
		m_membersCtrl->Enable(!readOnly);
	}

	ibFunctionalOptionEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		const ibTabDocument* document = nullptr
	);
};

#endif // !__FUNCTIONAL_OPTION_EDITOR_H__
