////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a field of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // the properties, as the frame carries them

#include "frmclient/win/ctrls/controlTextEditor.h"

void ibValueTextCtrl::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_textEditor = new ibControlNavigationTextEditor(parent, wxID_ANY);

	m_textEditor->Bind(wxEVT_CONTROL_BUTTON_SELECT, [this](wxCommandEvent&) { Send(ibProtocolEvent::Select); });
	m_textEditor->Bind(wxEVT_CONTROL_BUTTON_OPEN, [this](wxCommandEvent&) { Send(ibProtocolEvent::Open); });
	m_textEditor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, [this](wxCommandEvent&) { Send(ibProtocolEvent::Clear); });

	m_textEditor->Bind(wxEVT_CONTROL_TEXT_ENTER, &ibValueTextCtrl::OnTextEnter, this);
	m_textEditor->Bind(wxEVT_CONTROL_TEXT_INPUT, &ibValueTextCtrl::OnTextInput, this);
	m_textEditor->Bind(wxEVT_CONTROL_TEXT_CLEAR, &ibValueTextCtrl::OnTextInput, this);
	m_textEditor->Bind(wxEVT_KILL_FOCUS, &ibValueTextCtrl::OnKillFocus, this);
}

void ibValueTextCtrl::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	m_textEditor->SetLabel(state.GetString(ibProtocolName::Caption));

	// The value as text — none in the designer. Not over what is being typed: that is the person's until committed.
	if (state.Has(ibProtocolName::Text) && !m_editing) {
		const wxString text = state.GetString(ibProtocolName::Text);
		if (m_textEditor->GetValue() != text)
			m_textEditor->SetValue(text);
	}

	m_textEditor->SetPasswordMode(node.GetBool(ibProtocolName::PasswordMode));
	m_textEditor->SetMultilineMode(node.GetBool(ibProtocolName::MultilineMode));
	// The server has decided it: the form's text edit mode and a binding that can be written.
	m_textEditor->SetTextEditMode(!state.GetBool(ibProtocolName::ReadOnly));
	m_textEditor->ShowSelectButton(node.GetBool(ibProtocolName::ButtonSelect));
	m_textEditor->ShowOpenButton(node.GetBool(ibProtocolName::ButtonOpen));
	m_textEditor->ShowClearButton(node.GetBool(ibProtocolName::ButtonClear));

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
	args.SetValue(ibProtocolName::Text, m_textEditor->GetValue());
	Send(ibProtocolEvent::Change, args);
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibValueTextCtrl::OnTextInput(wxCommandEvent& event)
{
	// The first keystroke of an edit: the object is modified, nothing is committed.
	if (!m_editing) {
		m_editing = true;
		Send(ibProtocolEvent::Input);
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

//*******************************************************************************************
//*                  The properties — the desktop's ReadData / WriteData                          *
//*******************************************************************************************

bool ibValueTextCtrl::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyPasswordMode->SetNodeValue(node.GetProperty(m_propertyPasswordMode->GetName()));
	m_propertyMultilineMode->SetNodeValue(node.GetProperty(m_propertyMultilineMode->GetName()));
	m_propertyTexteditMode->SetNodeValue(node.GetProperty(m_propertyTexteditMode->GetName()));
	m_propertyFormat->SetNodeValue(node.GetProperty(m_propertyFormat->GetName()));
	m_propertySelectButton->SetNodeValue(node.GetProperty(m_propertySelectButton->GetName()));
	m_propertyOpenButton->SetNodeValue(node.GetProperty(m_propertyOpenButton->GetName()));
	m_propertyClearButton->SetNodeValue(node.GetProperty(m_propertyClearButton->GetName()));

	return ibValueWindow::ReadData(node);
}

bool ibValueTextCtrl::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyPasswordMode->GetName(), m_propertyPasswordMode->GetNodeValue());
	node.SetProperty(m_propertyMultilineMode->GetName(), m_propertyMultilineMode->GetNodeValue());
	node.SetProperty(m_propertyTexteditMode->GetName(), m_propertyTexteditMode->GetNodeValue());
	node.SetProperty(m_propertyFormat->GetName(), m_propertyFormat->GetNodeValue());
	node.SetProperty(m_propertySelectButton->GetName(), m_propertySelectButton->GetNodeValue());
	node.SetProperty(m_propertyOpenButton->GetName(), m_propertyOpenButton->GetNodeValue());
	node.SetProperty(m_propertyClearButton->GetName(), m_propertyClearButton->GetNodeValue());

	return ibValueWindow::WriteData(node);
}

