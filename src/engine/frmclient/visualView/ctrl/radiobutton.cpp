////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a radio button of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "gui/univ/ctrl/radioButton.h"

void ibValueRadioButton::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	// Each its own group: the server says which is selected, the buttons beside it are not unselected here (the univ
	// radio button groups its siblings up to the next wxRB_GROUP, and has no wxRB_SINGLE).
	m_radioButton = new ibRadioButton(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxRB_GROUP);

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

wxWindow* ibValueRadioButton::GetWindow() const
{
	return m_radioButton;
}

CONTROL_REGISTER(ibValueRadioButton, ibProtocol::ibForm::ibType::Radiobutton)
