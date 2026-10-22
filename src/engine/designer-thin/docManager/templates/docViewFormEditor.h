#ifndef _FORM_DESIGNER_H__
#define _FORM_DESIGNER_H__

#include "docViewModuleEditor.h"
#include "designer-thin/win/editor/visualEditor/visualEditor.h"

#include <wx/fdrepdlg.h>

// THE DESKTOP'S (designer/docManager/templates/docViewFormEditor.h) IN A TAB THE SERVER RAISED: the form's tab is a
// module's (frmserver ibFormDesignDocument over ibModuleTextDocument) whose form the CLIENT edits — the tab's one answer
// brings the form, its controls' rows, the classes a control may be made of and the module's text
// (ibProtocol::ibView::ibKind::FormDesigner); the form goes back whole as each command draws it (SaveForm) — the tab's
// save or close finds it handed.
// ⚠ No wx class info, as the module's tab has none.

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class ibFormEditView : public ibFrontendView {

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_visualNotebook->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_visualNotebook->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_visualNotebook->SelectAll(); }

	void OnFind(wxFindDialogEvent& event) {
		m_visualNotebook->FindText(event.GetFindString(), event.GetFlags());
	}

public:

	ibVisualEditorNotebook* GetVisualNotebook() const { return m_visualNotebook; }

public:

	ibFormEditView() : ibFrontendView(), m_visualNotebook(nullptr) {}

	virtual wxPrintout* OnCreatePrintout() override;
	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;
	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView) override;
	virtual void OnUpdate(ibFrontendView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

#if wxUSE_MENUS
	virtual wxMenuBar* CreateMenuBar() const override;
#endif // wxUSE_MENUS

protected:

	void OnMenuEvent(wxCommandEvent& event);

	// What was typed goes to the server as the focus leaves the text, as the module's tab hands it (ibModuleEditView).
	void OnKillFocus(wxFocusEvent& event);

private:

	struct ibControlData {

		wxString m_name;
		wxBitmap m_bmp;
		wxWindowID m_id;
	};

	std::vector< ibControlData> m_controlDataArray;

	ibVisualEditorNotebook* m_visualNotebook;

	wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// ibFormEditDocument: the server's form tab and the visual editor married
// ----------------------------------------------------------------------------

class ibFormEditDocument : public ibModuleEditDocument {
public:

	explicit ibFormEditDocument(long long tabId) : ibModuleEditDocument(tabId) {}

	// The module's State, and the card's: its title, and whether the form takes edits.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	virtual wxCommandProcessor* OnCreateCommandProcessor() override;

	virtual ibCodeEditor* GetCodeEditor() const override;

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) override;

	ibVisualEditorNotebook* GetVisualNotebook() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibFormEditDocument);
};

#endif
