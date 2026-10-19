////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a check box of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "frmclient/win/ctrls/controlCheckboxEditor.h"

void ibValueCheckbox::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_checkbox = new wxControlNavigationCheckbox(parent, wxID_ANY);

	// A read-only box never says it was toggled — the control puts the click back itself.
	m_checkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibEvent::Checked, event.IsChecked());
		Send(ibProtocol::ibEvent::ibKind::Toggle, args);
	});
}

void ibValueCheckbox::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	m_checkbox->SetLabel(state.GetString(ibProtocol::ibForm::ibState::Caption));
	m_checkbox->SetReadOnly(state.GetBool(ibProtocol::ibForm::ibState::ReadOnly));
	// The value — none in the designer.
	if (state.Has(ibProtocol::ibForm::ibState::Checked))
		m_checkbox->SetValue(state.GetBool(ibProtocol::ibForm::ibState::Checked));
	m_checkbox->SetWindowStyle(node.GetInt(ibProtocol::ibForm::ibProperty::TitleLocation)
		== static_cast<int>(ibProtocol::ibForm::ibProperty::ibTitleLocation::Left) ? wxALIGN_LEFT : wxALIGN_RIGHT);

	UpdateWindow(m_checkbox, node);
}

wxWindow* ibValueCheckbox::GetWindow() const
{
	return m_checkbox;
}

