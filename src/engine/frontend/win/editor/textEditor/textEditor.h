#ifndef __CODE_EDITOR_H__
#define __CODE_EDITOR_H__

#include <wx/cmdproc.h>
#include <wx/print.h>
#include <wx/stc/stc.h>

class ibTextCommandProcessor :
	public wxCommandProcessor {
public:

	ibTextCommandProcessor(wxStyledTextCtrl* codeEditor) :
		wxCommandProcessor(), m_codeEditor(codeEditor) {
	}

	virtual bool Undo() override {
		m_codeEditor->Undo();
		return true;
	}

	virtual bool Redo() override {
		m_codeEditor->Redo();
		return true;
	}

	virtual bool CanUndo() const override { return m_codeEditor->CanUndo(); }
	virtual bool CanRedo() const override { return m_codeEditor->CanRedo(); }

private:
	wxStyledTextCtrl* m_codeEditor;
};

#include "frontend/mainFrame/settings/editorsettings.h"
#include "frontend/mainFrame/settings/fontcolorsettings.h"

class ibTextEditor : public wxStyledTextCtrl {
public:

	ibTextEditor(class ibDocument *doc, wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxString& name = wxSTCNameStr);

	//Editor setting
	void SetEditorSettings(const ibEditorSettings& settings);

	//Font setting
	void SetFontColorSettings(const ibFontColorSettings& settings);

private:

	void OnTextChange(wxCommandEvent& event);

	//document — owning doc is just a back-link for SetModify on text change;
	// downgraded from ibMetaDocument* to ibDocument* after Text/Help docs
	// were decoupled from the metadata-bound base.
	ibDocument* m_document;
};


#endif