#ifndef _INTERFACE_H__
#define _INTERFACE_H__

#include "frontend/docView/docView.h"

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class ibInterfaceEditView : public ibMetaView {
	class ibInterfaceEditor* m_interfaceEditor;
public:

	ibInterfaceEditView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibInterfaceEditView);
};

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class ibInterfaceDocument : public ibMetaDocument
{
public:
	ibInterfaceDocument() : ibMetaDocument() { m_childDoc = false; }

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibInterfaceDocument);
	wxDECLARE_ABSTRACT_CLASS(ibInterfaceDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class ibInterfaceEditDocument : public ibInterfaceDocument
{
public:
	ibInterfaceEditDocument() : ibInterfaceDocument() { }

	wxDECLARE_NO_COPY_CLASS(ibInterfaceEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibInterfaceEditDocument);
};

#endif