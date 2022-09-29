#ifndef _INTERFACE_EDITOR_H__
#define _INTERFACE_EDITOR_H__

#include <wx/panel.h>
#include <wx/treectrl.h>

#include "common/docInfo.h"

#include "compiler/compiler.h"
#include "property/interfaceEditorProperty.h"

#include "frontend/theme/luna_auitoolbar.h"

class CInterfaceEditor : public wxPanel {

	enum
	{
		ID_MENUEDIT_NEW = 15000,
		ID_MENUEDIT_EDIT,
		ID_MENUEDIT_REMOVE,
		ID_MENUEDIT_PROPERTY,
	};

	CDocument* m_document;
	IMetaObject* m_metaObject;

	class wxInterfaceItemData : public wxTreeItemData {
		wxString m_caption; 
		eMenuType m_menuType;
	public:
		wxInterfaceItemData(eMenuType menuType) :
			m_menuType(menuType)
		{
		}
	};

	CInterfaceEditorProperty* m_interfaceProperty;

	wxAuiToolBar* m_metaTreeToolbar;
	wxTreeCtrl* m_menuCtrl;

	wxTreeItemId m_draggedItem;

	void OnCreateItem(wxCommandEvent& event);
	void OnEditItem(wxCommandEvent& event);
	void OnRemoveItem(wxCommandEvent& event);
	void OnPropertyItem(wxCommandEvent& event);

	void OnBeginDrag(wxTreeEvent& event);
	void OnEndDrag(wxTreeEvent& event);

	void OnRightClickItem(wxTreeEvent& event);
	void OnSelectedItem(wxTreeEvent& event);

private:

	wxTreeItemId AppendMenu(const wxTreeItemId& parent,
		wxStandardID id) const;
	wxTreeItemId AppendMenu(const wxTreeItemId& parent,
		const wxString& name) const;
	wxTreeItemId AppendSubMenu(const wxTreeItemId& parent,
		wxStandardID id) const;
	wxTreeItemId AppendSubMenu(const wxTreeItemId& parent,
		const wxString& name) const;
	wxTreeItemId AppendSeparator(const wxTreeItemId& parent) const;

	void CreateDefaultMenu();
	void FillMenu(const wxTreeItemId& parent, wxMenu *menu);

public:
	
	bool TestInterface();

public:

	void SetReadOnly(bool readOnly = true) {}

	CInterfaceEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY, CDocument *document = NULL, IMetaObject* metaObject = NULL);
	virtual ~CInterfaceEditor();
};

#endif 