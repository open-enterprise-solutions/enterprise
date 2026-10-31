
#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             Radiobutton                                  *
//****************************************************************************

ibValueRadioButton::ibValueRadioButton() : ibValueWindow()
{
}

void ibValueRadioButton::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);
	state.SetValue(ibProtocol::ibForm::ibState::Caption, GetCaption());
	state.SetValue(ibProtocol::ibForm::ibState::Selected, m_propertySelected->GetValueAsBoolean());
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool ibValueRadioButton::ReadData(const ibDataNode& node)
{
	// The caption and the starting selection are the radio button's own, so they travel with it - without
	// them every saved radio button came back as "Radio button", unselected.
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

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueRadioButton, "Radiobutton", "Widget", control_to_clsid("CT_RDBT"));
