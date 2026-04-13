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
		ibAccessObject* m_metaObject; //тип элемента
	public:
		wxTreeItemMetaData(ibAccessObject* metaObject) : m_metaObject(metaObject) {}
		ibAccessObject* GetMetaObject() const { return m_metaObject; }
	};

	class wxTreeItemRoleData : public wxTreeItemMetaData {
		ibRole* m_role; //тип элемента
	public:
		wxTreeItemRoleData(ibRole* role) : wxTreeItemMetaData(role->GetRoleObject()), m_role(role) {}
		ibRole* GetRole() const { return m_role; }
	};

	wxTreeCtrl* m_roleCtrl;
	ibCheckTree* m_checkCtrl;

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
		ibValueMetaObject* metaObject) const {
		wxImageList* imageList = m_roleCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		return m_roleCtrl->AppendItem(parent, metaObject->GetName(), imageIndex, imageIndex, new wxTreeItemMetaData(metaObject));
	}

	void AddInterfaceItem(ibValueMetaObject* obj, const wxTreeItemId& item);

	void InitRole();
	void ClearRole();

	void FillData();

public:

	void RefreshRole() {
		ClearRole();
		FillData();
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