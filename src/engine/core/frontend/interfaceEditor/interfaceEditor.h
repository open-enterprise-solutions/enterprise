#ifndef _INTERFACE_EDITOR_H__
#define _INTERFACE_EDITOR_H__

#include <wx/panel.h>
#include <wx/treectrl.h>

#include "frontend/docView/docView.h"

#include "core/compiler/compiler.h"
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
		CInterfaceEditorProperty* m_interfaceProperty;
	public:

		CInterfaceEditorProperty* GetProperty() const {
			return m_interfaceProperty;
		}

		wxInterfaceItemData(eMenuType menuType) :
			m_interfaceProperty(new CInterfaceEditorProperty(menuType)) {
		}

		wxInterfaceItemData(eMenuType menuType, const wxString &caption) :
			m_interfaceProperty(new CInterfaceEditorProperty(menuType)) {
		}

		virtual ~wxInterfaceItemData() {
			delete m_interfaceProperty;
		}
	};

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