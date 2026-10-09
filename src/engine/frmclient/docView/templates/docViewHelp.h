#ifndef __HELP_H__
#define __HELP_H__

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

#include "frmclient/docView/docView.h"
#include "frmclient/win/editor/textEditor/textEditor.h"

#include <wx/fdrepdlg.h>

// The view using a standard wxTextCtrl to show its contents
class FRMCLIENT_API ibFrontendHelpEditView : public ibFrontendView {
public:

	ibFrontendHelpEditView() : ibFrontendView(), m_textEditor(nullptr) {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	virtual wxPrintout* OnCreatePrintout() override;

	ibTextEditor* GetText() const { return m_textEditor; }

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_textEditor->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_textEditor->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_textEditor->SelectAll(); }

	void OnFind(wxFindDialogEvent& event);

	ibTextEditor* m_textEditor;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibFrontendHelpEditView);
};

// ----------------------------------------------------------------------------
// ibFrontendHelpDocument: ibFrontendDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

#include <wx/artprov.h>

class FRMCLIENT_API ibFrontendHelpDocument : public ibFrontendDocument
{
public:

	virtual wxIcon GetIcon() const {
		return wxArtProvider::GetIcon(wxART_HELP_BOOK, wxART_MENU);
	}

	ibFrontendHelpDocument() : ibFrontendDocument() {}

	virtual wxCommandProcessor* OnCreateCommandProcessor() override;
	virtual ibTextEditor* GetTextCtrl() const;

protected:
	wxDECLARE_NO_COPY_CLASS(ibFrontendHelpDocument);
	wxDECLARE_ABSTRACT_CLASS(ibFrontendHelpDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class FRMCLIENT_API ibFrontendHelpFileDocument : public ibFrontendHelpDocument
{
public:
	
	ibFrontendHelpFileDocument() : ibFrontendHelpDocument(), m_loadFromFile(false) {}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnNewDocument() override {

		// notice that there is no need to either reset nor even check the
		// modified flag here as the document itself is a new object (this is only
		// called from CreateDocument()) and so it shouldn't be saved anyhow even
		// if it is modified -- this could happen if the user code creates
		// documents pre-filled with some user-entered (and which hence must not be
		// lost) information

		SetDocumentSaved(false);

		const wxString name = GetDocumentManager()->MakeNewDocumentName();
	
		SetTitle(name);
		SetFilename(name, true);
		Modify(true);

		return true;
	}

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	bool m_loadFromFile;

	wxDECLARE_NO_COPY_CLASS(ibFrontendHelpFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibFrontendHelpFileDocument);
};

#endif