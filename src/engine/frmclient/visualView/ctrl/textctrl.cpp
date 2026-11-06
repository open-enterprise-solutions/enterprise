////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "gui/univ/ctrl/textCtrl.h"

void ibValueTextCtrl::Create(wxWindow* parent, const ibProtocolNode& node)
{
	// Many lines from the start where the node asks for them (SetMultiLine in Update changes the kind in place); Enter
	// commits what is typed.
	const long style = wxTE_PROCESS_ENTER
		| (node.GetBool(ibProtocol::ibForm::ibProperty::MultilineMode) ? wxTE_MULTILINE : 0);
	m_textCtrl = new ibTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, style);

	m_textCtrl->Bind(wxEVT_CONTROL_BUTTON_SELECT, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Select); });
	m_textCtrl->Bind(wxEVT_CONTROL_BUTTON_OPEN, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Open); });
	m_textCtrl->Bind(wxEVT_CONTROL_BUTTON_CLEAR, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Clear); });

	m_textCtrl->Bind(wxEVT_TEXT_ENTER, &ibValueTextCtrl::OnTextEnter, this);
	m_textCtrl->Bind(wxEVT_TEXT, &ibValueTextCtrl::OnTextInput, this);
	m_textCtrl->Bind(wxEVT_KILL_FOCUS, &ibValueTextCtrl::OnKillFocus, this);
}

void ibValueTextCtrl::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	m_textCtrl->SetTitle(state.GetString(ibProtocol::ibForm::ibState::Caption));

	// The value as text — none in the designer. Not over what is being typed: that is the person's until committed.
	// Changed, not set: nobody typed it (no wxEVT_TEXT, no Input).
	if (state.Has(ibProtocol::ibForm::ibState::Text) && !m_editing) {
		const wxString text = state.GetString(ibProtocol::ibForm::ibState::Text);
		if (m_textCtrl->GetValue() != text)
			m_textCtrl->ChangeValue(text);
	}

	m_textCtrl->SetPassword(node.GetBool(ibProtocol::ibForm::ibProperty::PasswordMode));
	m_textCtrl->SetMultiLine(node.GetBool(ibProtocol::ibForm::ibProperty::MultilineMode));
	// The server has decided it: the form's text edit mode and a binding that can be written.
	m_textCtrl->SetEditable(!state.GetBool(ibProtocol::ibForm::ibState::ReadOnly));
	m_textCtrl->ShowSelectButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonSelect));
	m_textCtrl->ShowOpenButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonOpen));
	m_textCtrl->ShowClearButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonClear));

	UpdateWindow(m_textCtrl, node);
}

wxWindow* ibValueTextCtrl::GetWindow() const
{
	return m_textCtrl;
}

bool ibValueTextCtrl::SaveEdit(ibViewEdit& edit) const
{
	if (!m_editing)
		return false;
	edit.text = m_textCtrl->GetValue();
	edit.insertion = m_textCtrl->GetInsertionPoint();
	return true;
}

void ibValueTextCtrl::RestoreEdit(const ibViewEdit& edit)
{
	// Still an edit: the server was told it started (Input), and is given it when it is committed.
	m_editing = true;
	m_textCtrl->ChangeValue(edit.text);
	m_textCtrl->SetInsertionPoint(edit.insertion);
	m_textCtrl->SetFocus();
}

void ibValueTextCtrl::Commit()
{
	m_editing = false;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Text, m_textCtrl->GetValue());
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
	if (m_editing && !m_textCtrl->IsBeingDeleted())
		Commit();
	event.Skip();
}

CONTROL_REGISTER(ibValueTextCtrl, ibProtocol::ibForm::ibType::Textctrl)
