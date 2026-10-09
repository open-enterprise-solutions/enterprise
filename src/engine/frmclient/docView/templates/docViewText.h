#ifndef __TEXT_H__
#define __TEXT_H__

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

#include "frmclient/docView/docView.h"
#include "frmclient/win/editor/textEditor/textEditor.h"

#include <wx/fdrepdlg.h>

// The view using a standard wxTextCtrl to show its contents
class FRMCLIENT_API ibFrontendTextEditView : public ibFrontendView {
public:

	ibFrontendTextEditView() : ibFrontendView(), m_textEditor(nullptr) {}

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

protected:

	ibTextEditor* m_textEditor;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibFrontendTextEditView);
};

// ----------------------------------------------------------------------------
// ibFrontendTextDocument: ibFrontendDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

#include <wx/artprov.h>

class FRMCLIENT_API ibFrontendTextDocument : public ibFrontendDocument
{
public:

	virtual wxIcon GetIcon() const {
		return wxArtProvider::GetIcon(wxART_NORMAL_FILE, wxART_MENU);
	}

	ibFrontendTextDocument() : ibFrontendDocument() {}

	virtual wxCommandProcessor* OnCreateCommandProcessor() override;
	virtual ibTextEditor* GetTextCtrl() const;

protected:
	wxDECLARE_NO_COPY_CLASS(ibFrontendTextDocument);
	wxDECLARE_ABSTRACT_CLASS(ibFrontendTextDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class FRMCLIENT_API ibFrontendTextFileDocument : public ibFrontendTextDocument
{
public:
	
	ibFrontendTextFileDocument() : ibFrontendTextDocument(), m_loadFromFile(false) {}

	// THE TEXT FORMAT, said once: the text template is registered with it (ibFrontendDocManager::RegisterDefaultTemplates),
	// and a text document made from no template saves by it — the way a spreadsheet answers its formats.
	static wxString FileMask() { return wxT("*.txt;*.text"); }
	static wxString FileExtensions() { return wxT("txt;text"); }
	virtual wxString GetSaveFilter() const override { return _("Text document") + wxT(" (") + FileMask() + wxT(")|") + FileMask(); }

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

	wxDECLARE_NO_COPY_CLASS(ibFrontendTextFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibFrontendTextFileDocument);
};

// ----------------------------------------------------------------------------
// The document and view a form's text box holds — the grid box's arrangement (the desktop's docViewSpreadsheet.h)
// ----------------------------------------------------------------------------

// A text file document in no manager's list and no tab of its own. The text is the editor's, and saving asks
// the editor, as the text file document does.
class FRMCLIENT_API ibTextBoxDocument : public ibFrontendTextFileDocument {
public:

	ibTextBoxDocument();

	// Save as of its own, by its own formats — the grid box document says why (the desktop's docViewSpreadsheet.h).
	virtual bool SaveAs() override;

	// The box holds this document, not its views — none of them going takes it along.
	virtual void OnChangedViewList() override {}

private:

	wxDECLARE_NO_COPY_CLASS(ibTextBoxDocument);
	wxDECLARE_DYNAMIC_CLASS(ibTextBoxDocument);
};

// Its view: created in the box's parent by the box's Create, closed and left empty by its Cleanup; its frame
// is that parent, so there is no title to mark.
class FRMCLIENT_API ibTextBoxView : public ibFrontendTextEditView {
public:

	ibTextBoxView() : ibFrontendTextEditView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual bool OnClose(bool deleteWindow = true) override;
	virtual void OnChangeFilename() override {}

private:
	wxDECLARE_DYNAMIC_CLASS(ibTextBoxView);
};

#endif