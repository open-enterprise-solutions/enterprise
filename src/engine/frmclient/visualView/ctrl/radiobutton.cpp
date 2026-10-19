////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a radio button of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibValueRadioButton::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_radioButton = new wxRadioButton(parent, wxID_ANY, wxEmptyString);
}

void ibValueRadioButton::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	m_radioButton->SetLabel(state.GetString(ibProtocol::ibForm::ibState::Caption));
	m_radioButton->SetValue(state.GetBool(ibProtocol::ibForm::ibState::Selected));

	UpdateWindow(m_radioButton, node);
}
