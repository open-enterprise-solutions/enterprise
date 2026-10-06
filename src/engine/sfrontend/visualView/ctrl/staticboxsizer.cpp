
#include "sizer.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)


//****************************************************************************
//*                             StaticBoxSizer                               *
//****************************************************************************

ibValueStaticBoxSizer::ibValueStaticBoxSizer() : ibValueSizer()
{
}

//****************************************************************************
//*                               Update                                     *
//****************************************************************************

void ibValueStaticBoxSizer::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// Orientation, size, font, colours and the tooltip are the schema's: the client reads them off the
	// properties as saved. The caption is resolved here into the user's language; a frame the functional
	// options of this base make unavailable is not shown, whatever its own Visible says.
	state.SetValue(wxT("Title"), m_propertyTitle->GetValueAsTranslateString());
	state.SetValue(wxT("Enabled"), m_propertyEnabled->GetValueAsBoolean());
	state.SetValue(wxT("Visible"), m_propertyVisible->GetValueAsBoolean() && IsAvailable());
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************



bool ibValueStaticBoxSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));	
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyFont->SetNodeValue(node.GetProperty(m_propertyFont->GetName()));
	m_propertyFG->SetNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->SetNodeValue(node.GetProperty(m_propertyBG->GetName()));

	m_propertyTooltip->SetNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyContextHelp->SetNodeValue(node.GetProperty(m_propertyContextHelp->GetName()));

	m_propertyContextMenu->SetNodeValue(node.GetProperty(m_propertyContextMenu->GetName()));
	m_propertyEnabled->SetNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueSizer::ReadData(node);
}

bool ibValueStaticBoxSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyFont->GetName(), m_propertyFont->GetNodeValue());
	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyContextHelp->GetName(), m_propertyContextHelp->GetNodeValue());
	node.SetProperty(m_propertyContextMenu->GetName(), m_propertyContextMenu->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStaticBoxSizer, "Staticboxsizer", "Sizer", control_to_clsid("CT_SSZER"));
