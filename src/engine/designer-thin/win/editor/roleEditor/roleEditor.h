#ifndef _ROLE_EDITOR_H__
#define _ROLE_EDITOR_H__

#include <wx/panel.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>

#include "frmclient/docView/tabDocument.h"
#include "frmclient/win/ctrls/checktree.h"

// THE DESKTOP'S (designer/win/editor/roleEditor) OVER THE SERVER'S ROWS: every object that has rights, and beside it
// the rights the role gives on the one selected, as the tab's view writes them (frmserver ibRoleRightsView, ViewKind
// Role) — each row with its rights (Roles). A tick goes to the server (Toggle, naming the right) and comes back drawn.

class ibRoleEditor : public wxSplitterWindow {

	wxTreeItemId m_treeMETADATA;

	// The tab — what a tick is posted to.
	const ibTabDocument* m_document;

	class ibTreeItemObject : public wxTreeItemData {
		long long m_metaId; // the row's object — its NodeId
		ibProtocolNode m_roles; // …and its rights, as the role gives them
	public:
		ibTreeItemObject(long long metaId, const ibProtocolNode& roles) : m_metaId(metaId), m_roles(roles) {}
		long long GetMetaId() const { return m_metaId; }
		const ibProtocolNode& GetRoles() const { return m_roles; }
	};

	class wxTreeItemRoleData : public ibTreeItemObject {
		wxString m_role; // the right — its Name
	public:
		wxTreeItemRoleData(long long metaId, const wxString& role) : ibTreeItemObject(metaId, ibProtocolNode()), m_role(role) {}
		const wxString& GetRole() const { return m_role; }
	};

	wxTreeCtrl* m_roleCtrl;
	ibCheckTree* m_checkCtrl;

	long long m_keepSelId = 0;   // the object selected before a RefreshRole — restored after
	wxTreeItemId m_keepSelNode;  // its node in the rebuilt tree (captured during AppendItem)
	long long m_keepTopId = 0;   // …and the row at the top — a tick is drawn back by a rebuild, the list stays where it was
	wxTreeItemId m_keepTopNode;

protected:

	void OnCheckItem(wxTreeEvent& event);
	void OnSelectedItem(wxTreeEvent& event);

private:

	// A metatype's group row, as the server wrote it.
	wxTreeItemId GroupFor(const ibProtocolNode& row);

	wxTreeItemId AppendItem(const wxTreeItemId& parent, const ibProtocolNode& row);

	void AddInterfaceItem(const ibProtocolNode& row, const wxTreeItemId& item);

	void InitRole();
	void ClearRole();

	void FillData(const ibProtocolNode& view);

public:

	void RefreshRole(const ibProtocolNode& view);

	void SetReadOnly(bool readOnly = true) {
		m_checkCtrl->Enable(!readOnly);
	}

	ibRoleEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		const ibTabDocument* document = nullptr
	);

	virtual ~ibRoleEditor();
};

#endif
