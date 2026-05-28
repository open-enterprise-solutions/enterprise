#ifndef __HELP_H__
#define __HELP_H__

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

#include "frontend/docView/docView.h"
#include "frontend/win/editor/textEditor/textEditor.h"

#include <wx/fdrepdlg.h>

// The view using a standard wxTextCtrl to show its contents
class FRONTEND_API ibHelpEditView : public ibView {
public:

	ibHelpEditView() : ibView(), m_textEditor(nullptr) {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
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
	wxDECLARE_DYNAMIC_CLASS(ibHelpEditView);
};

// ----------------------------------------------------------------------------
// ibHelpDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

// metaModuleObject.h is included for g_metaCommonModuleCLSID (icon lookup
// only — not for metadata binding; ibHelpDocument is a plain file document).
#include "backend/metaCollection/metaModuleObject.h"

class FRONTEND_API ibHelpDocument : public ibDocument
{
public:

	virtual wxIcon GetIcon() const {
		return ibBackendPicture::GetPictureAsIcon(g_metaCommonModuleCLSID);
	}

	ibHelpDocument() : ibDocument() {}

	virtual wxCommandProcessor* OnCreateCommandProcessor() override;
	virtual ibTextEditor* GetTextCtrl() const;

protected:
	wxDECLARE_NO_COPY_CLASS(ibHelpDocument);
	wxDECLARE_ABSTRACT_CLASS(ibHelpDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class FRONTEND_API ibHelpFileDocument : public ibHelpDocument
{
public:
	
	ibHelpFileDocument() : ibHelpDocument(), m_loadFromFile(false) {}

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

	wxDECLARE_NO_COPY_CLASS(ibHelpFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibHelpFileDocument);
};

#endif