#ifndef _MODULE_H__
#define _MODULE_H__

#include "frmclient/docView/tabDocument.h"
#include "frmclient/win/editor/codeEditor/codeEditor.h"

#include <wx/fdrepdlg.h>

// THE DESKTOP'S (designer/docManager/templates/docViewModuleEditor.h) IN A TAB THE SERVER RAISED: the module is the
// server's document (frmserver ibModuleTextDocument) over the metaobject — modified, saved and closed there; this one
// shows its text in the code editor, which fetches it and hands it back (LoadModule / SaveModule). ⚠ No wx class info:
// frmserver, loaded into this process for a file base, registers its own pair.

// The view using a standard wxTextCtrl to show its contents
class ibModuleEditView : public ibFrontendView {
public:

	ibModuleEditView() : ibFrontendView(), m_codeEditor(nullptr) {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;
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

	// What was typed goes to the server as the focus leaves the text, as a form's text box hands its own.
	void OnKillFocus(wxFocusEvent& event);

protected:

	ibCodeEditor* m_codeEditor;

	wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// ibModuleEditDocument: the server's module tab and the code editor married
// ----------------------------------------------------------------------------

class ibModuleEditDocument : public ibTabDocument {
public:

	explicit ibModuleEditDocument(long long tabId) : ibTabDocument(tabId) {}

	// ⚠ MODIFIED IS THE CONFIGURATION'S, as the view's State says it — not the tab's Save, which a module's tab offers all
	// along (its document never modified, the desktop's either): Save read as "modified" sent no key's Input at all.
	virtual bool IsModified() const override { return m_modified; }
	// ⚠ THE FIRST KEY SAYS IT TO THE SERVER — the tab's Input: the configuration modified there, the text handed back
	// later (SaveModule). Once the configuration is modified, the keys say nothing more.
	virtual void Modify(bool modify) override;

	// What was typed goes before the server saves or closes the tab — as the desktop wrote the module on save.
	virtual bool Close() override;
	virtual bool Save() override;

	// The view's State: the syntax the module is written in, read-only where the configuration may not be edited, and
	// whether the configuration is modified.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	virtual wxCommandProcessor* OnCreateCommandProcessor() override;

	// The module's editor — the view's own; a form's tab has it on its notebook's Code page (ibFormEditDocument).
	virtual ibCodeEditor* GetCodeEditor() const;

	// The debugger's, as the desktop's document took them (ibModuleEditDocument): the line shown — the run line
	// (setBreakpoint) or a failure's (a form's tab turns to its Code page first); and the value of what is under the
	// cursor while stopped.
	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint);
	virtual void SetToolTip(const wxString& resultStr);

	// THE MODULE'S ADDRESS — the State's Module: what its breakpoints are kept by and a runtime stops at.
	const wxString& GetDocPath() const { return m_docPath; }

protected:

	virtual ibFrontendView* DoCreateView() override;

private:

	// The State's Modified — set at the first key already, before the server's answer says it.
	bool m_modified = false;
	// The State's Module.
	wxString m_docPath;

	wxDECLARE_NO_COPY_CLASS(ibModuleEditDocument);
};

#endif
