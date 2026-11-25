////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view - on or off (Flag)
////////////////////////////////////////////////////////////////////////////

#include "field.h"

#include "deskgui/univ/ctrl/checkBox.h"

void ibFieldViewFlag::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_checkbox = new ibCheckBox(parent, wxID_ANY, wxEmptyString);

	// A box that is not editable never says it was toggled — its press changes nothing.
	m_checkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibEvent::Checked, event.IsChecked());
		Send(ibProtocol::ibEvent::ibKind::Toggle, args);
	});
}

void ibFieldViewFlag::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	m_checkbox->SetEditable(!state.GetBool(ibProtocol::ibForm::ibState::ReadOnly));
	// The value — none in the studio.
	if (state.Has(ibProtocol::ibForm::ibState::Checked))
		m_checkbox->SetValue(state.GetBool(ibProtocol::ibForm::ibState::Checked));
}

ibControl* ibFieldViewFlag::GetControl() const
{
	return m_checkbox;
}
