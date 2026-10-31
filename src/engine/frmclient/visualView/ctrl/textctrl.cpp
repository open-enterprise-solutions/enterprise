////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "frmclient/win/ctrls/controlTextEditor.h"

void ibValueTextCtrl::Create(wxWindow* parent, const ibProtocolNode& node)
{
	// Multiline is a creation-time style on macOS / GTK: passed here, as the desktop does — the later
	// SetMultilineMode() in Update() cannot add it to an existing native control.
	const long style = wxBORDER_NONE
		| (node.GetBool(ibProtocol::ibForm::ibProperty::MultilineMode) ? wxTE_MULTILINE : 0);
	m_textEditor = new ibControlNavigationTextEditor(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, style);

	m_textEditor->Bind(wxEVT_CONTROL_BUTTON_SELECT, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Select); });
	m_textEditor->Bind(wxEVT_CONTROL_BUTTON_OPEN, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Open); });
	m_textEditor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Clear); });

	m_textEditor->Bind(wxEVT_CONTROL_TEXT_ENTER, &ibValueTextCtrl::OnTextEnter, this);
	m_textEditor->Bind(wxEVT_CONTROL_TEXT_INPUT, &ibValueTextCtrl::OnTextInput, this);
	m_textEditor->Bind(wxEVT_CONTROL_TEXT_CLEAR, &ibValueTextCtrl::OnTextInput, this);
	m_textEditor->Bind(wxEVT_KILL_FOCUS, &ibValueTextCtrl::OnKillFocus, this);
}

void ibValueTextCtrl::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	m_textEditor->SetLabel(state.GetString(ibProtocol::ibForm::ibState::Caption));

	// The value as text — none in the designer. Not over what is being typed: that is the person's until committed.
	if (state.Has(ibProtocol::ibForm::ibState::Text) && !m_editing) {
		const wxString text = state.GetString(ibProtocol::ibForm::ibState::Text);
		if (m_textEditor->GetValue() != text)
			m_textEditor->SetValue(text);
	}

	m_textEditor->SetPasswordMode(node.GetBool(ibProtocol::ibForm::ibProperty::PasswordMode));
	m_textEditor->SetMultilineMode(node.GetBool(ibProtocol::ibForm::ibProperty::MultilineMode));
	// The server has decided it: the form's text edit mode and a binding that can be written.
	m_textEditor->SetTextEditMode(!state.GetBool(ibProtocol::ibForm::ibState::ReadOnly));
	m_textEditor->ShowSelectButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonSelect));
	m_textEditor->ShowOpenButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonOpen));
	m_textEditor->ShowClearButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonClear));

	UpdateWindow(m_textEditor, node);
}

wxWindow* ibValueTextCtrl::GetWindow() const
{
	return m_textEditor;
}

bool ibValueTextCtrl::SaveEdit(ibViewEdit& edit) const
{
	if (!m_editing)
		return false;
	edit.text = m_textEditor->GetValue();
	edit.insertion = m_textEditor->GetInsertionPoint();
	return true;
}

void ibValueTextCtrl::RestoreEdit(const ibViewEdit& edit)
{
	// Still an edit: the server was told it started (Input), and is given it when it is committed.
	m_editing = true;
	m_textEditor->SetValue(edit.text);
	m_textEditor->SetInsertionPoint(edit.insertion);
	m_textEditor->SetFocus();
}

void ibValueTextCtrl::Commit()
{
	m_editing = false;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Text, m_textEditor->GetValue());
	Send(ibProtocol::ibEvent::ibKind::Change, args);
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibValueTextCtrl::OnTextInput(wxCommandEvent& event)
{
	// The first keystroke of an edit: the object is modified, nothing is committed.
	if (!m_editing) {
		m_editing = true;
		Send(ibProtocol::ibEvent::ibKind::Input);
	}
	event.Skip();
}

void ibValueTextCtrl::OnTextEnter(wxCommandEvent& event)
{
	Commit();
	event.Skip();
}

void ibValueTextCtrl::OnKillFocus(wxFocusEvent& event)
{
	// A field torn down loses its focus too — what was typed there goes with it.
	if (m_editing && !m_textEditor->IsBeingDeleted())
		Commit();
	event.Skip();
}

