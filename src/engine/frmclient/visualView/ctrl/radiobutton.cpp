////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a radio button of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibValueRadioButton::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	// Single: the server says which is selected, the buttons beside it are not unselected by wx.
	m_radioButton = new wxRadioButton(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxRB_SINGLE);

	m_radioButton->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) {
		if (m_readOnly) {
			m_radioButton->SetValue(false);
			return;
		}
		Send(ibProtocol::ibEvent::ibKind::Toggle, ibProtocolNode());
	});
}

void ibValueRadioButton::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	m_radioButton->SetLabel(state.GetString(ibProtocol::ibForm::ibState::Caption));
	m_radioButton->SetValue(state.GetBool(ibProtocol::ibForm::ibState::Selected));
	m_readOnly = state.GetBool(ibProtocol::ibForm::ibState::ReadOnly);

	UpdateWindow(m_radioButton, node);
}
