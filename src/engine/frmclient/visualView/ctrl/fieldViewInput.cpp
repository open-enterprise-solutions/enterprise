////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view - typed into (Input)
////////////////////////////////////////////////////////////////////////////

#include "field.h"

#include "deskgui/univ/ctrl/textCtrl.h"

void ibFieldViewInput::Create(wxWindow* parent, const ibProtocolNode& node)
{
	// Many lines from the start where the node asks for them (SetMultiLine in Update changes the kind in place); Enter
	// commits what is typed.
	const long style = wxTE_PROCESS_ENTER
		| (node.GetBool(ibProtocol::ibForm::ibProperty::MultilineMode) ? wxTE_MULTILINE : 0);
	m_textCtrl = new ibTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, style);

	m_textCtrl->Bind(wxEVT_CONTROL_BUTTON_SELECT, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Select); });
	m_textCtrl->Bind(wxEVT_CONTROL_BUTTON_OPEN, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Open); });
	m_textCtrl->Bind(wxEVT_CONTROL_BUTTON_CLEAR, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Clear); });

	m_textCtrl->Bind(wxEVT_TEXT_ENTER, &ibFieldViewInput::OnTextEnter, this);
	m_textCtrl->Bind(wxEVT_TEXT, &ibFieldViewInput::OnTextInput, this);
	m_textCtrl->Bind(wxEVT_SET_FOCUS, &ibFieldViewInput::OnSetFocus, this);
	m_textCtrl->Bind(wxEVT_KILL_FOCUS, &ibFieldViewInput::OnKillFocus, this);
}

void ibFieldViewInput::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	// What can be typed into it — the server resolved it (docs/private/input-mask.md); none when it says none.
	m_textCtrl->SetMask(ibInputMask::Parse(state.GetString(ibProtocol::ibForm::ibState::Mask)));

	// The value as text — none in the designer. Not over what is being typed: that is the person's until committed.
	// Changed, not set: nobody typed it (no wxEVT_TEXT, no Input).
	if (state.Has(ibProtocol::ibForm::ibState::Text))
		m_text = state.GetString(ibProtocol::ibForm::ibState::Text);
	m_editText = state.GetString(ibProtocol::ibForm::ibState::EditText);
	if (!m_editing)
		ShowText(m_textCtrl->HasFocus());

	m_textCtrl->SetPassword(node.GetBool(ibProtocol::ibForm::ibProperty::PasswordMode));
	m_textCtrl->SetMultiLine(node.GetBool(ibProtocol::ibForm::ibProperty::MultilineMode));
	// The server has decided whether it may be written (a binding that can be); typed into — its text edit mode too. Off,
	// the value is still picked and cleared (Max, 2026-11-24): Select and Clear given back where the text is locked.
	const bool writable = !state.GetBool(ibProtocol::ibForm::ibState::ReadOnly);
	m_textCtrl->SetEditable(writable && node.GetBool(ibProtocol::ibForm::ibProperty::TexteditMode, true));
	m_textCtrl->ShowSelectButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonSelect));
	m_textCtrl->ShowOpenButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonOpen));
	m_textCtrl->ShowClearButton(node.GetBool(ibProtocol::ibForm::ibProperty::ButtonClear));
	m_textCtrl->EnableSelectButton(writable);
	m_textCtrl->EnableClearButton(writable);
}

ibControl* ibFieldViewInput::GetControl() const
{
	return m_textCtrl;
}

bool ibFieldViewInput::SaveEdit(ibViewEdit& edit) const
{
	if (!m_editing)
		return false;
	edit.text = m_textCtrl->GetValue();
	edit.insertion = m_textCtrl->GetInsertionPoint();
	return true;
}

void ibFieldViewInput::RestoreEdit(const ibViewEdit& edit)
{
	// Still an edit: the server was told it started (Input), and is given it when it is committed.
	m_editing = true;
	m_textCtrl->ChangeValue(edit.text);
	m_textCtrl->SetInsertionPoint(edit.insertion);
	m_textCtrl->SetFocus();
}

void ibFieldViewInput::Commit()
{
	m_editing = false;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Text, m_textCtrl->GetValue());
	Send(ibProtocol::ibEvent::ibKind::Change, args);
}

void ibFieldViewInput::ShowText(bool focused)
{
	wxString text = m_text;
	const ibInputMask& mask = m_textCtrl->GetMask();
	if (!mask.IsEmpty()) {
		if (focused)
			text = mask.Apply(ibString(!m_editText.empty() ? m_editText : m_text)).ToWxString();
		else if (text.empty())
			text = mask.Apply(ibString()).ToWxString();
	}
	if (m_textCtrl->GetValue() != text)
		m_textCtrl->ChangeValue(text);
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibFieldViewInput::OnTextInput(wxCommandEvent& event)
{
	// The first keystroke of an edit: the object is modified, nothing is committed.
	if (!m_editing) {
		m_editing = true;
		Send(ibProtocol::ibEvent::ibKind::Input);
	}
	event.Skip();
}

void ibFieldViewInput::OnTextEnter(wxCommandEvent& event)
{
	Commit();
	event.Skip();
}

// Coming into a masked field shows the mask: the text it is typed over, laid into it, all of it selected — the first key
// starts the value anew.
void ibFieldViewInput::OnSetFocus(wxFocusEvent& event)
{
	if (!m_editing && !m_textCtrl->GetMask().IsEmpty()) {
		ShowText(true);
		m_textCtrl->SelectAll();
	}
	event.Skip();
}

void ibFieldViewInput::OnKillFocus(wxFocusEvent& event)
{
	// A field torn down loses its focus too — what was typed there goes with it. Left untouched, it shows its text at
	// rest again.
	if (!m_textCtrl->IsBeingDeleted()) {
		if (m_editing)
			Commit();
		else
			ShowText(false);
	}
	event.Skip();
}
