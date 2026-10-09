////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a caption of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // the properties, as the frame carries them

#include "frmclient/win/ctrls/controlStaticTextValue.h"

void ibValueStaticText::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	// ONE widget for both cases, as on the desktop: bound, a caption and a value; unbound, the value half is empty and
	// takes no room. Only the value sends a click, and only where it is a link.
	m_staticText = new ibControlStaticTextValue(parent, wxID_ANY);
	m_staticText->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Send(ibProtocolEvent::Open); });
}

void ibValueStaticText::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	m_staticText->SetLabel(state.GetString(ibProtocolName::Caption));
	m_staticText->SetValueText(state.GetString(ibProtocolName::Text));
	m_staticText->SetHyperlink(state.GetBool(ibProtocolName::Link));
	m_staticText->SetWindowStyle(node.GetInt(ibProtocolName::TitleLocation) == 1 ? wxALIGN_LEFT : wxALIGN_RIGHT);

	UpdateWindow(m_staticText, node);
}

wxWindow* ibValueStaticText::GetWindow() const
{
	return m_staticText;
}

//*******************************************************************************************
//*                  The properties — the desktop's ReadData / WriteData                          *
//*******************************************************************************************

bool ibValueStaticText::ReadData(const ibDataNode& node)
{
	m_propertyTitleLocation->SetNodeValue(node.GetProperty(m_propertyTitleLocation->GetName()));
	m_propertyMarkup->SetNodeValue(node.GetProperty(m_propertyMarkup->GetName()));
	m_propertyWrap->SetNodeValue(node.GetProperty(m_propertyWrap->GetName()));
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueStaticText::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitleLocation->GetName(), m_propertyTitleLocation->GetNodeValue());
	node.SetProperty(m_propertyMarkup->GetName(), m_propertyMarkup->GetNodeValue());
	node.SetProperty(m_propertyWrap->GetName(), m_propertyWrap->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());

	return ibValueWindow::WriteData(node);
}

