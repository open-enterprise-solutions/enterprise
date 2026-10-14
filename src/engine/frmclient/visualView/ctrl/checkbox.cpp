////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
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
		args.SetValue(ibProtocolName::Checked, event.IsChecked());
		Send(ibProtocolEvent::Toggle, args);
	});
}

void ibValueCheckbox::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	m_checkbox->SetLabel(state.GetString(ibProtocolName::Caption));
	m_checkbox->SetReadOnly(state.GetBool(ibProtocolName::ReadOnly));
	// The value — none in the designer.
	if (state.Has(ibProtocolName::Checked))
		m_checkbox->SetValue(state.GetBool(ibProtocolName::Checked));
	m_checkbox->SetWindowStyle(node.GetInt(ibProtocolName::TitleLocation) == 1 ? wxALIGN_LEFT : wxALIGN_RIGHT);

	UpdateWindow(m_checkbox, node);
}

wxWindow* ibValueCheckbox::GetWindow() const
{
	return m_checkbox;
}

