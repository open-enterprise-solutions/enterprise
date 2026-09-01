#ifndef _IB_DIALOG_TEXT_EDITOR_H_
#define _IB_DIALOG_TEXT_EDITOR_H_

////////////////////////////////////////////////////////////////////////////
//	Description : editing one of a metaobject's two texts — help, or notes
////////////////////////////////////////////////////////////////////////////
//
// ⭐ ONE DIALOG, TWO CALLERS, AND THE CAPTION IS THE ONLY DIFFERENCE. Help and notes are
// different things to write and are chosen apart in the menu, but EDITING text is the same act
// either way — a second dialog would be the same code under another name, free to drift in
// margins, in shortcuts and in what the Cancel button means.
//
// ⭐ AND IT IS A STYLED TEXT CONTROL, not a plain field. Both texts are written as markdown and
// read as prose: a person needs a caret that behaves, tabs that indent, undo that goes back more
// than one step, and a monospaced face so a table or a code fence lines up. wxTextCtrl gives none
// of that, and the platform already owns the other one — the module editor is built on it.
//
// Deliberately NOT the code editor (ibCodeEditor): that one carries a lexer, a compile context,
// autocomplete and a document behind it. None of those mean anything for prose, and inheriting
// them would tie a note to the script machinery for the sake of a text box.
//
////////////////////////////////////////////////////////////////////////////

#include <wx/dialog.h>
#include <wx/stc/stc.h>

class ibDialogTextEditor : public wxDialog {
public:

	// `caption` is what the person is looking at — "Help information", "Notes". The dialog does
	// not know which of the two it holds, and does not need to.
	ibDialogTextEditor(wxWindow* parent, const wxString& caption,
		const wxString& subject, const wxString& text);

	// What was typed. Read after ShowModal() answers wxID_OK; unchanged text is still returned,
	// so a caller that always writes back is not wrong, only wasteful.
	wxString GetText() const;

private:

	wxStyledTextCtrl* m_text = nullptr;
};

#endif // !_IB_DIALOG_TEXT_EDITOR_H_
