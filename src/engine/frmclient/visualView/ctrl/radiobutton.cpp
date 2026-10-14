////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a radio button of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibValueRadioButton::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_radioButton = new wxRadioButton(parent, wxID_ANY, wxEmptyString);
}

void ibValueRadioButton::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	m_radioButton->SetLabel(state.GetString(ibProtocolName::Caption));
	m_radioButton->SetValue(state.GetBool(ibProtocolName::Selected));

	UpdateWindow(m_radioButton, node);
}
