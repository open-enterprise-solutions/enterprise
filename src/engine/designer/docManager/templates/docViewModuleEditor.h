#ifndef _MODULE_H__
#define _MODULE_H__

#include "frontend/docView/docView.h"
#include "win/editor/codeEditor/codeEditorDesigner.h"

#include <wx/fdrepdlg.h>

// The view using a standard wxTextCtrl to show its contents
class ibModuleEditView : public ibMetaView {
public:

	ibModuleEditView() : ibMetaView(), m_codeEditor(nullptr) {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint = nullptr) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	virtual wxPrintout* OnCreatePrintout() override;
	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;

	ibCodeEditor* GetCodeEditor() const { return m_codeEditor; }

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_codeEditor->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_codeEditor->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_codeEditor->SelectAll(); }
	void OnFind(wxFindDialogEvent& event) { m_codeEditor->FindText(event.GetFindString(), event.GetFlags()); }

	void OnMenuEvent(wxCommandEvent& event);

protected:

	ibCodeEditorDesigner* m_codeEditor;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibModuleEditView);
};

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class ibModuleDocument : public ibValueModuleDocument {
public:

	ibModuleDocument() : ibValueModuleDocument() {}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnOpenDocument(const wxString& filename) override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnSaveModified() override;
	virtual bool OnCloseDocument() override;
	virtual wxCommandProcessor* OnCreateCommandProcessor() override;
	
	virtual ibCodeEditor* GetCodeEditor() const = 0;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual bool Save() override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibModuleDocument);
	wxDECLARE_ABSTRACT_CLASS(ibModuleDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class ibModuleEditDocument : public ibModuleDocument {
public:
	ibModuleEditDocument() : ibModuleDocument() {}

	virtual ibCodeEditor* GetCodeEditor() const override;

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) override;
	virtual void SetToolTip(const wxString& resultStr) override;
	virtual void ShowAutoComplete(const struct ibDebugAutoCompleteData& debugData) override;

	wxDECLARE_NO_COPY_CLASS(ibModuleEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibModuleEditDocument);
};

#endif