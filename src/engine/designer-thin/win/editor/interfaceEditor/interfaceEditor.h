#ifndef __INTERFACE_EDITOR_H__
#define __INTERFACE_EDITOR_H__

#include <wx/treectrl.h>

#include "frmclient/docView/tabDocument.h"
#include "frmclient/win/ctrls/checktree.h"

// THE DESKTOP'S (designer/win/editor/interfaceEditor) OVER THE SERVER'S ROWS: the section's objects, ticked where it
// keeps them, as the tab's view writes them (frmserver ibInterfaceCompositionView, ViewKind Interface) — the
// configuration's row, a group per metatype, its objects. A tick goes to the server (Toggle) and comes back drawn.

class ibInterfaceEditor : public wxWindow {

	wxTreeItemId m_treeMETADATA;

	// The tab — what a tick is posted to.
	const ibTabDocument* m_document;

	class ibTreeItemObject : public wxTreeItemData {
		long long m_metaId; // the row's object — its NodeId
	public:
		ibTreeItemObject(long long metaId) : m_metaId(metaId) {}
		long long GetMetaId() const { return m_metaId; }
	};

	ibCheckTree* m_interfaceCtrl;

	long long m_keepSelId = 0;   // the object selected before a RefreshInterface — restored after
	wxTreeItemId m_keepSelNode;  // its node in the rebuilt tree (captured during AppendItem)
	long long m_keepTopId = 0;   // …and the row at the top — a tick is drawn back by a rebuild, the list stays where it was
	wxTreeItemId m_keepTopNode;

protected:

	void OnCheckItem(wxTreeEvent& event);

private:

	// A metatype's group row, as the server wrote it.
	wxTreeItemId GroupFor(const ibProtocolNode& row);

	wxTreeItemId AppendItem(const wxTreeItemId& parent, const ibProtocolNode& row);

	void InitInterface();
	void ClearInterface();

	void FillData(const ibProtocolNode& view);

public:

	void RefreshInterface(const ibProtocolNode& view);

	void SetReadOnly(bool readOnly = true) {
		m_interfaceCtrl->Enable(!readOnly);
	}

	ibInterfaceEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		const ibTabDocument* document = nullptr
	);
};

#endif
