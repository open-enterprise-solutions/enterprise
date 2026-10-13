////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a radio button of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // the properties, as the frame carries them

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

//*******************************************************************************************
//*                  The properties — the desktop's ReadData / WriteData                    *
//*******************************************************************************************

bool ibValueRadioButton::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertySelected->SetNodeValue(node.GetProperty(m_propertySelected->GetName()));

	return ibValueWindow::ReadData(node);
}

bool ibValueRadioButton::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertySelected->GetName(), m_propertySelected->GetNodeValue());

	return ibValueWindow::WriteData(node);
}
