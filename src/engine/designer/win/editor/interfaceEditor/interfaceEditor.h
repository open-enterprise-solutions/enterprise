#ifndef __INTERFACE_EDITOR_H__
#define __INTERFACE_EDITOR_H__

#include <wx/treectrl.h>
#include <wx/splitter.h>

#include "backend/metadataConfiguration.h"

#include "frontend/win/theme/luna_toolbarart.h"
#include "frontend/win/ctrls/checktree.h"

class ibInterfaceEditor : public wxWindow {

	wxTreeItemId m_treeMETADATA;
	wxTreeItemId m_treeCOMMON; //special tree

	wxTreeItemId m_treeFORMS;
	wxTreeItemId m_treeCOMMANDS;   // general commands — check one INTO this section so it renders in the section menu

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

	ibValueMetaObject* m_metaInterface;

	class wxTreeItemMetaData : public wxTreeItemData {
		ibInterfaceObject* m_metaObject; // element type
	public:
		wxTreeItemMetaData(ibInterfaceObject* metaObject) : m_metaObject(metaObject) {}
		ibInterfaceObject* GetMetaObject() const { return m_metaObject; }
	};

	ibCheckTree* m_interfaceCtrl;

	ibInterfaceObject* m_keepSelObj = nullptr;   // the metaobject selected before a RefreshInterface — restored after
	wxTreeItemId m_keepSelNode;                  // its node in the rebuilt tree (captured during AppendItem)

protected:

	void OnCheckItem(wxTreeEvent& event);

private:

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const ibClassID& clsid, const wxString& name = wxEmptyString) const {
		const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
		wxASSERT(typeCtor);
		wxImageList* imageList = m_interfaceCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(typeCtor->GetClassIcon());
		return m_interfaceCtrl->AppendItem(parent, name.IsEmpty() ? typeCtor->GetClassName() : name, imageIndex, imageIndex, nullptr);
	}

	wxTreeItemId AppendItem(const wxTreeItemId& parent,
		ibValueMetaObject* metaObject) {
		wxImageList* imageList = m_interfaceCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		// Bare NAME (GetName) — the item already sits UNDER its type group ("Documents" / "Data processors" / …), so the
		// metatype-qualified GetFullName ("Document.Document1") just repeats the group. Mirrors the role editor's tree.
		wxTreeItemId createItem = m_interfaceCtrl->AppendItem(parent, metaObject->GetName(), imageIndex, imageIndex, new wxTreeItemMetaData(metaObject));
		m_interfaceCtrl->SetItemState(createItem,
			metaObject->IsSetInterface(m_metaInterface->GetMetaID()) ?
			metaObject->IsEditable() ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED :
			metaObject->IsEditable() ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED
		);
		if (metaObject == m_keepSelObj)   // the row that was selected before this rebuild — remember it for restore
			m_keepSelNode = createItem;

		return createItem;
	}

	void InitInterface();
	void ClearInterface();

	void FillData();

public:

	void RefreshInterface() {
		// Preserve the current row across the full rebuild — capture the selected metaobject by identity, re-select it
		// after refilling. A section edited elsewhere skips its OWN doc (OnCheckItem); only OTHER open section editors
		// rebuild here, and each keeps its row (the check state itself is re-read from the metaobject either way).
		m_keepSelObj = nullptr;
		if (const wxTreeItemId sel = m_interfaceCtrl->GetSelection(); sel.IsOk())
			if (const wxTreeItemMetaData* d = dynamic_cast<wxTreeItemMetaData*>(m_interfaceCtrl->GetItemData(sel)))
				m_keepSelObj = d->GetMetaObject();
		m_keepSelNode = wxTreeItemId();
		ClearInterface();
		FillData();
		if (m_keepSelNode.IsOk())
			m_interfaceCtrl->SelectItem(m_keepSelNode);       // a metaobject row — restored by identity
	}

	void SetReadOnly(bool readOnly = true) {
		m_interfaceCtrl->Enable(!readOnly);
	}

	ibInterfaceEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		ibValueMetaObject* metaObject = nullptr
	);
};

#endif 