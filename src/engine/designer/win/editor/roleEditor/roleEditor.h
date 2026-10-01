#ifndef _ROLE_EDITOR_H__
#define _ROLE_EDITOR_H__

#include <wx/panel.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>


#include "backend/metadataConfiguration.h"

#include "frontend/win/theme/luna_toolbarart.h"
#include "frontend/win/ctrls/checktree.h"

class ibRoleEditor : public wxSplitterWindow {

	wxTreeItemId m_treeMETADATA;

	// One group per metatype, created on demand by GroupFor and captioned from the type
	// registry — the thirteen named branches this class used to declare are gone with the
	// blocks that filled them.
	std::map<ibClassID, wxTreeItemId> m_groups;

	ibValueMetaObject* m_metaRole;

	class ibTreeItemObject : public wxTreeItemData {
		ibAccessObject* m_metaObject; // element type
	public:
		ibTreeItemObject(ibAccessObject* metaObject) : m_metaObject(metaObject) {}
		ibAccessObject* GetMetaObject() const { return m_metaObject; }
	};

	class wxTreeItemRoleData : public ibTreeItemObject {
		ibRole* m_role; // element type
	public:
		wxTreeItemRoleData(ibRole* role) : ibTreeItemObject(role->GetRoleObject()), m_role(role) {}
		ibRole* GetRole() const { return m_role; }
	};

	wxTreeCtrl* m_roleCtrl;
	ibCheckTree* m_checkCtrl;

	ibAccessObject* m_keepSelObj = nullptr;   // the metaobject selected before a RefreshRole — restored after
	wxTreeItemId m_keepSelNode;               // its node in the rebuilt tree (captured during AppendItem)

protected:

	void OnCheckItem(wxTreeEvent& event);
	void OnSelectedItem(wxTreeEvent& event);

private:

	// A group per metatype, created on first use.
	wxTreeItemId GroupFor(const ibClassID& clsid);

	wxTreeItemId AppendItem(const wxTreeItemId& parent,
		ibValueMetaObject* metaObject) {
		wxImageList* imageList = m_roleCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		const wxTreeItemId item = m_roleCtrl->AppendItem(parent, metaObject->GetName(), imageIndex, imageIndex, new ibTreeItemObject(metaObject));
		if (metaObject == m_keepSelObj)   // the row that was selected before this rebuild — remember it for restore
			m_keepSelNode = item;
		return item;
	}

	void AddInterfaceItem(ibValueMetaObject* obj, const wxTreeItemId& item);

	void InitRole();
	void ClearRole();

	void FillData();

public:

	void RefreshRole() {
		// Preserve the current row across the full rebuild — capture the selected metaobject by identity, re-select it
		// after refilling (the check tree then re-follows via OnSelectedItem). A role edited elsewhere skips its OWN
		// doc (OnCheckItem), so only OTHER open role editors rebuild here, and each keeps its row.
		m_keepSelObj = nullptr;
		if (const wxTreeItemId sel = m_roleCtrl->GetSelection(); sel.IsOk())
			if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_roleCtrl->GetItemData(sel)))
				m_keepSelObj = d->GetMetaObject();
		m_keepSelNode = wxTreeItemId();

		// ONE FRAME — the rebuild empties the tree and refills it, and unfrozen that shows
		// as a collapse followed by a re-appear. Same pattern as the form's attribute tree.
		m_roleCtrl->Freeze();
		m_roleCtrl->SetEvtHandlerEnabled(false);

		ClearRole();
		FillData();
		if (m_keepSelNode.IsOk())
			m_roleCtrl->SelectItem(m_keepSelNode);            // a metaobject row — restored by identity
		else if (m_keepSelObj != nullptr && m_treeMETADATA.IsOk())
			m_roleCtrl->SelectItem(m_treeMETADATA);           // the config root (built in InitRole, not via AppendItem)

		m_roleCtrl->SetEvtHandlerEnabled(true);
		m_roleCtrl->Thaw();
	}

	void SetReadOnly(bool readOnly = true) {
		m_checkCtrl->Enable(!readOnly);
	}

	ibRoleEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		ibValueMetaObject* metaObject = nullptr
	);

	virtual ~ibRoleEditor();
};

#endif 