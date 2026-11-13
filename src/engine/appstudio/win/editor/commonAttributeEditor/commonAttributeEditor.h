#ifndef __COMMON_ATTRIBUTE_EDITOR_H__
#define __COMMON_ATTRIBUTE_EDITOR_H__

// THE COMPOSITION OF A COMMON ATTRIBUTE — which objects carry it: the desktop's
// (designer/win/editor/commonAttributeEditor) over the server's rows, as the tab's view writes them (frmserver
// ibCommonAttributeCompositionView, ViewKind CommonAttribute). The tree is built by ASKING, there: every metaobject that
// answers IsCompositionAllowed() comes, grouped under its own type.
//
// Checking a box is not a note about intent — it puts a real attribute inside that
// object (and unchecking removes it), the server's SetCompositionObject; the tree is drawn back from what it did.

#include <wx/treectrl.h>

#include "frmclient/docView/tabDocument.h"
#include "frmclient/win/ctrls/checktree.h"

class ibCommonAttributeCompositionEditor : public wxWindow {

	wxTreeItemId m_treeMETADATA;

	// The tab — what a tick is posted to.
	const ibTabDocument* m_document;

	class ibTreeItemObject : public wxTreeItemData {
		long long m_metaId; // the row's object — its NodeId
	public:
		ibTreeItemObject(long long metaId) : m_metaId(metaId) {}
		long long GetMetaId() const { return m_metaId; }
	};

	ibCheckTree* m_compositionCtrl;

	long long m_keepSelId = 0;
	wxTreeItemId m_keepSelNode;
	long long m_keepTopId = 0;   // the row at the top — a tick is drawn back by a rebuild, the list stays where it was
	wxTreeItemId m_keepTopNode;

protected:

	void OnCheckItem(wxTreeEvent& event);

private:

	// A metatype's group row, as the server wrote it — the same caption the designer tree reads.
	wxTreeItemId GroupFor(const ibProtocolNode& row);

	wxTreeItemId AppendItem(const wxTreeItemId& parent, const ibProtocolNode& row);

	void InitComposition();
	void ClearComposition();
	void FillData(const ibProtocolNode& view);

public:

	void RefreshComposition(const ibProtocolNode& view);

	void SetReadOnly(bool readOnly = true) {
		m_compositionCtrl->Enable(!readOnly);
	}

	ibCommonAttributeCompositionEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		const ibTabDocument* document = nullptr
	);
};

#endif // !__COMMON_ATTRIBUTE_EDITOR_H__
