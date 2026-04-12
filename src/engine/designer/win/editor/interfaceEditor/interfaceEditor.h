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

	wxTreeItemId m_treeCONSTANTS;

	wxTreeItemId m_treeCATALOGS;
	wxTreeItemId m_treeDOCUMENTS;
	wxTreeItemId m_treeDATAPROCESSORS;
	wxTreeItemId m_treeREPORTS;
	wxTreeItemId m_treeINFORMATION_REGISTERS;
	wxTreeItemId m_treeACCUMULATION_REGISTERS;

	ibValueMetaObject* m_metaInterface;

	class wxTreeItemMetaData : public wxTreeItemData {
		ibInterfaceObject* m_metaObject; //тип элемента
	public:
		wxTreeItemMetaData(ibInterfaceObject* metaObject) : m_metaObject(metaObject) {}
		ibInterfaceObject* GetMetaObject() const { return m_metaObject; }
	};

	ibCheckTree* m_interfaceCtrl;

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
		ibValueMetaObject* metaObject) const {
		wxImageList* imageList = m_interfaceCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		wxTreeItemId createItem = m_interfaceCtrl->AppendItem(parent, metaObject->GetName(), imageIndex, imageIndex, new wxTreeItemMetaData(metaObject));
		m_interfaceCtrl->SetItemState(createItem,
			metaObject->IsSetInterface(m_metaInterface->GetMetaID()) ?
			metaObject->IsEditable() ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED :
			metaObject->IsEditable() ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED
		);

		return createItem;
	}

	void InitInterface();
	void ClearInterface();

	void FillData();

public:

	void RefreshInterface() {
		ClearInterface();
		FillData();
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