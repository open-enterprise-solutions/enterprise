#ifndef _ROLE_H__
#define _ROLE_H__

#include "frontend/docView/docView.h"

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class ibRoleEditView : public ibMetaView {
	class ibRoleEditor* m_roleEditor;
public:
	ibRoleEditView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibRoleEditView);
};

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class ibRoleDocument : public ibMetaDocument
{
public:
	ibRoleDocument() : ibMetaDocument() { /*m_childDoc = false;*/ }

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibRoleDocument);
	wxDECLARE_ABSTRACT_CLASS(ibRoleDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class ibRoleEditDocument : public ibRoleDocument
{
public:
	ibRoleEditDocument() : ibRoleDocument() { }

	wxDECLARE_NO_COPY_CLASS(ibRoleEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibRoleEditDocument);
};

#endif