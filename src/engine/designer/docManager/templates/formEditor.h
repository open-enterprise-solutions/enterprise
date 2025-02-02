#ifndef _FORM_DESIGNER_H__
#define _FORM_DESIGNER_H__

#include "docManager/docManager.h"
#include "win/editor/visualEditor/visualEditor.h"

#include <wx/fdrepdlg.h>

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class CFormEditView : public CMetaView {
	CVisualEditorNotebook* m_visualNotebook;
private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) {
		m_visualNotebook->Copy();
	}

	void OnPaste(wxCommandEvent& WXUNUSED(event)) {
		m_visualNotebook->Paste();
	}

	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) {
		m_visualNotebook->SelectAll();
	}

	void OnFind(wxFindDialogEvent& event) {
		m_visualNotebook->FindText(
			event.GetFindString(),
			event.GetFlags()
		);
	}

public:

	CVisualEditorNotebook* GetVisualNotebook() const {
		return m_visualNotebook;
	}

public:

	CFormEditView() :
		CMetaView() {
	}

	virtual bool OnCreate(CMetaDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, wxView* activeView, wxView* deactiveView) override;
	virtual void OnUpdate(wxView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	virtual wxPrintout* OnCreatePrintout() override;

	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;
	virtual void OnRemoveToolbar(wxAuiToolBar* toolbar) override;

#if wxUSE_MENUS		
	virtual wxMenu* CreateViewMenu() const;
	virtual void OnMenuItemClicked(int id);
#endif // wxUSE_MENUS

protected:

	void OnMenuClicked(wxCommandEvent& event);

private:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CFormEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CFormDocument : public IModuleDocument {
public:

	CFormDocument() :
		IModuleDocument() {
	}

	virtual wxCommandProcessor* CreateCommandProcessor() const override;

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnOpenDocument(const wxString& filename) override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnSaveModified() override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;
	virtual bool Save() override;

	virtual CVisualEditorNotebook* GetVisualNotebook() const = 0;

protected:

	wxDECLARE_NO_COPY_CLASS(CFormDocument);
	wxDECLARE_ABSTRACT_CLASS(CFormDocument);
};

class CFormEditDocument : public CFormDocument
{
public:
	CFormEditDocument() :
		CFormDocument() {
	}

	virtual CVisualEditorNotebook* GetVisualNotebook() const override;

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) override {
		CVisualEditorNotebook* visualNotebook = GetVisualNotebook();
		if (visualNotebook != nullptr && visualNotebook->GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR) {
			visualNotebook->SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		}

		CCodeEditor* autoComplete = GetCodeEditor();
		wxASSERT(autoComplete);
		autoComplete->SetCurrentLine(lineBreakpoint, setBreakpoint);
	}

	virtual void SetToolTip(const wxString& resultStr) override {
		CVisualEditorNotebook* visualNotebook = GetVisualNotebook();
		if (visualNotebook != nullptr && visualNotebook->GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR) {
			visualNotebook->SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		}
		CCodeEditor* codeEditor = GetCodeEditor();
		wxASSERT(codeEditor);
		if (codeEditor != nullptr) {
			codeEditor->SetToolTip(resultStr);
		}
	}

	virtual void ShowAutoComplete(const debugAutoCompleteData_t& debugData) override {
		CVisualEditorNotebook* visualNotebook = GetVisualNotebook();
		if (visualNotebook != nullptr && visualNotebook->GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR) {
			visualNotebook->SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		}
		CCodeEditor* codeEditor = GetCodeEditor();
		wxASSERT(codeEditor);
		if (codeEditor != nullptr) {
			codeEditor->ShowAutoComplete(debugData);
		}
	}

	virtual CCodeEditor* GetCodeEditor() const {
		CVisualEditorNotebook* visualNotebook = GetVisualNotebook();
		if (visualNotebook != nullptr) {
			return visualNotebook->GetCodeEditor();
		}
		return nullptr;
	};

	wxDECLARE_NO_COPY_CLASS(CFormEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CFormEditDocument);
};

#endif