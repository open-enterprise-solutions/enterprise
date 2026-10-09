////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a check box of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // the properties, as the frame carries them

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

//*******************************************************************************************
//*                  The properties — the desktop's ReadData / WriteData                          *
//*******************************************************************************************

bool ibValueCheckbox::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyTitleLocation->SetNodeValue(node.GetProperty(m_propertyTitleLocation->GetName()));

	return ibValueWindow::ReadData(node);
}

bool ibValueCheckbox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyTitleLocation->GetName(), m_propertyTitleLocation->GetNodeValue());

	return ibValueWindow::WriteData(node);
}

