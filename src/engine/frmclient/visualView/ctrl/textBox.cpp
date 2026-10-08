////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a text box of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/editor/textEditor/textEditor.h"

void ibValueTextBox::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	// The desktop's text document view — its editor, with the window's settings; the text is the server's.
	m_textEditor = new ibTextEditor(nullptr, parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
	m_textEditor->SetEditorSettings(mainFrame->GetEditorSettings());
	m_textEditor->SetFontColorSettings(mainFrame->GetFontColorSettings());

	m_textEditor->Bind(wxEVT_KILL_FOCUS, &ibValueTextBox::OnKillFocus, this);
}

void ibValueTextBox::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	// The text — not over what is being typed: that is the person's until committed.
	if (state.Has(ibProtocolName::Text) && m_textEditor->GetText() == m_text) {
		m_text = state.GetString(ibProtocolName::Text);
		if (m_textEditor->GetText() != m_text) {
			m_textEditor->SetText(m_text);
			m_textEditor->EmptyUndoBuffer();
		}
	}
	m_textEditor->SetReadOnly(state.GetBool(ibProtocolName::ReadOnly));

	UpdateWindow(m_textEditor, node);
}

wxWindow* ibValueTextBox::GetWindow() const
{
	return m_textEditor;
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibValueTextBox::OnKillFocus(wxFocusEvent& event)
{
	// A box torn down loses its focus too — what was typed there goes with it.
	if (!m_textEditor->IsBeingDeleted() && m_textEditor->GetText() != m_text) {
		m_text = m_textEditor->GetText();

		ibProtocolNode args;
		args.SetValue(ibProtocolName::Text, m_text);
		Send(ibProtocolEvent::Change, args);
	}
	event.Skip();
}
