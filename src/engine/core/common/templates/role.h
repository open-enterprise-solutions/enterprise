#ifndef _ROLE_H__
#define _ROLE_H__

#include "common/docInfo.h"

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class CRoleEditView : public CView {
	class CRoleEditor* m_roleEditor;
public:
	CRoleEditView() : CView() {}

	virtual bool OnCreate(CDocument* doc, long flags) override;
	virtual void OnUpdate(wxView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CRoleEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CRoleDocument : public CDocument
{
public:
	CRoleDocument() : CDocument() { m_childDoc = false; }

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CRoleDocument);
	wxDECLARE_ABSTRACT_CLASS(CRoleDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class CRoleEditDocument : public CRoleDocument
{
public:
	CRoleEditDocument() : CRoleDocument() { }

	wxDECLARE_NO_COPY_CLASS(CRoleEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CRoleEditDocument);
};

#endif