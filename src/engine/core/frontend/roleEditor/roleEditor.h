#ifndef _ROLE_EDITOR_H__
#define _ROLE_EDITOR_H__

#include <wx/panel.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>

#include "compiler/compiler.h"
#include "metadata/metadata.h"
#include "frontend/theme/luna_auitoolbar.h"

#include "frontend/controls/checktree.h"

class CRoleEditor : public wxSplitterWindow {

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

	IMetaObject* m_metaRole;

	class wxTreeItemMetaData : public wxTreeItemData {
		IMetaObject* m_metaObject; //тип элемента
	public:
		wxTreeItemMetaData(IMetaObject* metaObject) : m_metaObject(metaObject) {}
		IMetaObject* GetMetaObject() const {
			return m_metaObject;
		}
	};

	class wxTreeItemRoleData : public wxTreeItemMetaData {
		Role* m_role; //тип элемента
	public:
		wxTreeItemRoleData(Role* role) : wxTreeItemMetaData(role->GetObject()), m_role(role) {}
		Role* GetRole() const {
			return m_role;
		}
	};

	wxTreeCtrl* m_roleCtrl;
	wxCheckTree* m_checkCtrl;

protected:

	void OnCheckItem(wxTreeEvent& event);
	void OnSelectedItem(wxTreeEvent& event);

private:

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const CLASS_ID& clsid, const wxString& name = wxEmptyString) const {
		IObjectValueAbstract* singleObject = CValue::GetAvailableObject(clsid);
		wxASSERT(singleObject);
		wxImageList* imageList = m_roleCtrl->GetImageList();
		wxASSERT(imageList);
		int imageIndex = imageList->Add(singleObject->GetClassIcon());
		return m_roleCtrl->AppendItem(parent, name.IsEmpty() ? singleObject->GetClassName() : name, imageIndex, imageIndex, NULL);
	}

	wxTreeItemId AppendItem(const wxTreeItemId& parent,
		IMetaObject* metaObject) const {
		wxImageList* imageList = m_roleCtrl->GetImageList();
		wxASSERT(imageList);
		int imageIndex = imageList->Add(metaObject->GetIcon());
		return m_roleCtrl->AppendItem(parent, metaObject->GetName(), imageIndex, imageIndex, new wxTreeItemMetaData(metaObject));
	}

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

	CRoleEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		IMetaObject* metaObject = NULL
	);

	virtual ~CRoleEditor();
};

#endif 