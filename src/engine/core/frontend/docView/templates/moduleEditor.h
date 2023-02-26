#ifndef _MODULE_H__
#define _MODULE_H__

#include "frontend/docView/docView.h"
#include "frontend/codeEditor/codeEditorCtrl.h"

#include <wx/fdrepdlg.h>

// The view using a standard wxTextCtrl to show its contents
class CModuleEditView : public CView
{
	CCodeEditorCtrl* m_codeEditor;

public:

	CModuleEditView() : CView(), m_codeEditor(NULL) {}

	virtual bool OnCreate(CDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual void OnUpdate(wxView* sender, wxObject* hint = NULL) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	virtual wxPrintout* OnCreatePrintout() override;

	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;
	virtual void OnRemoveToolbar(wxAuiToolBar* toolbar) override;

	CCodeEditorCtrl* GetCodeEditor() const {
		return m_codeEditor;
	}

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) {
		m_codeEditor->Copy();
	}

	void OnPaste(wxCommandEvent& WXUNUSED(event)) {
		m_codeEditor->Paste();
	}

	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) {
		m_codeEditor->SelectAll();
	}

	void OnFind(wxFindDialogEvent& event) {
		m_codeEditor->FindText(
			event.GetFindString(),
			event.GetFlags()
		);
	}

	void OnMenuClicked(wxCommandEvent& event);

protected:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CModuleEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CModuleDocument : public IModuleDocument {
public:

	CModuleDocument() : IModuleDocument() { }

	virtual wxCommandProcessor* CreateCommandProcessor() const override;

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnOpenDocument(const wxString& filename) override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnSaveModified() override;
	virtual bool OnCloseDocument() override;

	virtual CCodeEditorCtrl* GetCodeEditor() const = 0;

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

	virtual CCodeEditorCtrl* GetCodeEditor() const override;
	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) override;

	wxDECLARE_NO_COPY_CLASS(CModuleEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CModuleEditDocument);
};

#endif