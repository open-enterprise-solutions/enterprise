////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a check box of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "gui/univ/ctrl/checkBox.h"

void ibValueCheckbox::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_checkbox = new ibCheckBox(parent, wxID_ANY, wxEmptyString);

	// A box that is not editable never says it was toggled — its press changes nothing.
	m_checkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibEvent::Checked, event.IsChecked());
		Send(ibProtocol::ibEvent::ibKind::Toggle, args);
	});
}

void ibValueCheckbox::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	// The caption is the box's title, in a column on its side — the left one lined up with the fields' titles.
	m_checkbox->SetTitle(state.GetString(ibProtocol::ibForm::ibState::Caption));
	m_checkbox->SetTitlePosition(node.GetInt(ibProtocol::ibForm::ibProperty::TitleLocation)
		== static_cast<int>(ibProtocol::ibForm::ibProperty::ibTitleLocation::Left) ? wxLEFT : wxRIGHT);
	m_checkbox->SetEditable(!state.GetBool(ibProtocol::ibForm::ibState::ReadOnly));
	// The value — none in the designer.
	if (state.Has(ibProtocol::ibForm::ibState::Checked))
		m_checkbox->SetValue(state.GetBool(ibProtocol::ibForm::ibState::Checked));

	UpdateWindow(m_checkbox, node);
}

wxWindow* ibValueCheckbox::GetWindow() const
{
	return m_checkbox;
}

CONTROL_REGISTER(ibValueCheckbox, ibProtocol::ibForm::ibType::Checkbox)
