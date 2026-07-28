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
	wxTreeItemId m_treeCOMMON; //special tree

	wxTreeItemId m_treeFORMS;
	wxTreeItemId m_treeINTERFACES;

	wxTreeItemId m_treeCONSTANTS;

	wxTreeItemId m_treeCATALOGS;
	wxTreeItemId m_treeDOCUMENTS;
	wxTreeItemId m_treeDATAPROCESSORS;
	wxTreeItemId m_treeREPORTS;
	wxTreeItemId m_treeINFORMATION_REGISTERS;
	wxTreeItemId m_treeACCUMULATION_REGISTERS;
	wxTreeItemId m_treeCHARTS_OF_CHARACTERISTIC_TYPES;
	wxTreeItemId m_treeCHARTS_OF_ACCOUNTS;
	wxTreeItemId m_treeACCOUNTING_REGISTERS;

	ibValueMetaObject* m_metaRole;

	class wxTreeItemMetaData : public wxTreeItemData {
		ibAccessObject* m_metaObject; // element type
	public:
		wxTreeItemMetaData(ibAccessObject* metaObject) : m_metaObject(metaObject) {}
		ibAccessObject* GetMetaObject() const { return m_metaObject; }
	};

	class wxTreeItemRoleData : public wxTreeItemMetaData {
		ibRole* m_role; // element type
	public:
		wxTreeItemRoleData(ibRole* role) : wxTreeItemMetaData(role->GetRoleObject()), m_role(role) {}
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

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const ibClassID& clsid, const wxString& name = wxEmptyString) const {
		const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
		wxASSERT(typeCtor);
		wxImageList* imageList = m_roleCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(typeCtor->GetClassIcon());
		return m_roleCtrl->AppendItem(parent, name.IsEmpty() ? typeCtor->GetClassName() : name, imageIndex, imageIndex, nullptr);
	}

	wxTreeItemId AppendItem(const wxTreeItemId& parent,
		ibValueMetaObject* metaObject) {
		wxImageList* imageList = m_roleCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		const wxTreeItemId item = m_roleCtrl->AppendItem(parent, metaObject->GetName(), imageIndex, imageIndex, new wxTreeItemMetaData(metaObject));
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
			if (const wxTreeItemMetaData* d = dynamic_cast<wxTreeItemMetaData*>(m_roleCtrl->GetItemData(sel)))
				m_keepSelObj = d->GetMetaObject();
		m_keepSelNode = wxTreeItemId();
		ClearRole();
		FillData();
		if (m_keepSelNode.IsOk())
			m_roleCtrl->SelectItem(m_keepSelNode);            // a metaobject row — restored by identity
		else if (m_keepSelObj != nullptr && m_treeMETADATA.IsOk())
			m_roleCtrl->SelectItem(m_treeMETADATA);           // the config root (built in InitRole, not via AppendItem)
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