#ifndef _MODULE_H__
#define _MODULE_H__

#include "common/docInfo.h"
#include "frontend/codeEditor/codeEditorCtrl.h"

#include <wx/fdrepdlg.h>

// The view using a standard wxTextCtrl to show its contents
class CModuleView : public CView
{
	CCodeEditorCtrl* m_code;

public:

	CModuleView() : CView(), m_code(NULL) {}

	virtual bool OnCreate(CDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual void OnUpdate(wxView* sender, wxObject* hint = NULL) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	virtual wxPrintout* OnCreatePrintout() override;

	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;
	virtual void OnRemoveToolbar(wxAuiToolBar* toolbar) override;

	CCodeEditorCtrl* GetText() const { return m_code; }

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_code->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_code->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_code->SelectAll(); }

	void OnFind(wxFindDialogEvent& event);

	void OnMenuClicked(wxCommandEvent& event);

protected:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CModuleView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CModuleDocument : public CDocument
{
public:

	CModuleDocument() : CDocument() { }

	virtual CCommandProcessor* CreateCommandProcessor() override;

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnOpenDocument(const wxString& filename) override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnSaveModified() override;
	virtual bool OnCloseDocument() override;

	virtual CCodeEditorCtrl* GetTextCtrl() const = 0;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual bool Save() override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CModuleDocument);
	wxDECLARE_ABSTRACT_CLASS(CModuleDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class CModuleEditDocument : public CModuleDocument {
public:
	CModuleEditDocument() : CModuleDocument() { }
	virtual CCodeEditorCtrl* GetTextCtrl() const override;

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) override;

	wxDECLARE_NO_COPY_CLASS(CModuleEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CModuleEditDocument);
};

#endif