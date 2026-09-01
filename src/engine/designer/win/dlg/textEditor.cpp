////////////////////////////////////////////////////////////////////////////
//	Description : editing one of a metaobject's two texts — help, or notes
////////////////////////////////////////////////////////////////////////////

#include "textEditor.h"

#include <wx/sizer.h>
#include <wx/settings.h>

ibDialogTextEditor::ibDialogTextEditor(wxWindow* parent, const wxString& caption,
	const wxString& subject, const wxString& text)
	: wxDialog(parent, wxID_ANY,
		// The SUBJECT is in the title, because a dialog that says only "Notes" leaves a person
		// who opened two of them in a row unable to tell which object they are looking at.
		// ⚠ ASCII IN THE LITERAL. This file carries NO BOM, so MSVC reads it as the system code
		// page (1251 here) rather than UTF-8: a UTF-8 em-dash arrives as its three separate bytes
		// and reaches the title bar as mojibake. The neighbouring assistant view gets away with
		// one only because that file HAS a BOM — which is exactly why the rule has to be about the
		// literal and not about remembering which file is which.
		subject.IsEmpty() ? caption : caption + wxT(" - ") + subject,
		wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	m_text = new wxStyledTextCtrl(this, wxID_ANY);

	// PROSE, NOT CODE. No lexer is set on purpose — markdown highlighting would colour a
	// paragraph by guessing at it, and a wrong guess in a note is worse than no colour. What is
	// borrowed from the editor is only what prose actually needs.
	m_text->SetLexer(wxSTC_LEX_NULL);

	// A monospaced face so a table or a fenced block lines up; taken from the system rather than
	// named, since a hard-coded family is the thing that looks wrong on the one machine nobody
	// tested on.
	wxFont face = wxSystemSettings::GetFont(wxSYS_ANSI_FIXED_FONT);
	if (face.IsOk())
		m_text->StyleSetFont(wxSTC_STYLE_DEFAULT, face);
	m_text->StyleClearAll();

	// ⭐ WRAPPED, because these are sentences. A note is read, not scrolled sideways — and the
	// one habit borrowed from the code editor that would be wrong here is a horizontal scrollbar.
	m_text->SetWrapMode(wxSTC_WRAP_WORD);
	m_text->SetMarginWidth(0, 0);   // no line numbers: nobody cites a note by line
	m_text->SetMarginWidth(1, 0);   // …and no fold margin either
	m_text->SetUseTabs(false);
	m_text->SetTabWidth(4);

	m_text->SetText(text);
	m_text->EmptyUndoBuffer();      // opening is not an edit to be undone
	m_text->SetFocus();

	wxBoxSizer* layout = new wxBoxSizer(wxVERTICAL);
	layout->Add(m_text, 1, wxEXPAND | wxALL, FromDIP(6));

	if (wxSizer* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL))
		layout->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(6));

	SetSizer(layout);
	SetSize(FromDIP(wxSize(640, 460)));
	CentreOnParent();
}

wxString ibDialogTextEditor::GetText() const
{
	return m_text != nullptr ? m_text->GetText() : wxString();
}
