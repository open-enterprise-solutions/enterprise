#ifndef _INTERFACE_H__
#define _INTERFACE_H__

#include "frontend/docView/docView.h"

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class CInterfaceEditView : public CMetaView {
	class CInterfaceEditor* m_interfaceEditor;
public:

	CInterfaceEditView() : CMetaView() {}

	virtual bool OnCreate(CMetaDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, wxView* activeView, wxView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

#if wxUSE_MENUS		
	virtual wxMenu* CreateViewMenu() const;
	virtual void OnMenuItemClicked(int id);
#endif // wxUSE_MENUS

private:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CInterfaceEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CInterfaceDocument : public CMetaDocument
{
public:
	CInterfaceDocument() : CMetaDocument() { m_childDoc = false; }

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CInterfaceDocument);
	wxDECLARE_ABSTRACT_CLASS(CInterfaceDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class CInterfaceEditDocument : public CInterfaceDocument
{
public:
	CInterfaceEditDocument() : CInterfaceDocument() { }

	wxDECLARE_NO_COPY_CLASS(CInterfaceEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CInterfaceEditDocument);
};

#endif