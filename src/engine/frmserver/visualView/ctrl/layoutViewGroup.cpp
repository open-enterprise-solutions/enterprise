#include "layout.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//****************************************************************************
//*                                 Group                                    *
//****************************************************************************

ibString ibLayoutViewGroup::GetControlTitle() const
{
	if (!m_propertyTitle->IsEmptyProperty())
		return m_propertyTitle->GetValueAsTranslateString();

	return ibLayoutViewProvider::GetControlTitle();
}

void ibLayoutViewGroup::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// Orientation, font, colours and the tooltip are the schema's: the client reads them off the properties as saved.
	// The caption is resolved here into the user's language; a frame the feature options of this base make unavailable
	// is not shown, whatever its own Visible says.
	state.SetValue(ibProtocol::ibForm::ibState::Title, m_propertyTitle->GetValueAsTranslateString());
	state.SetValue(ibProtocol::ibForm::ibState::Enabled, m_propertyEnabled->GetValueAsBoolean());
	state.SetValue(ibProtocol::ibForm::ibState::Visible, m_propertyVisible->GetValueAsBoolean() && GetLayout()->IsAvailable());
}

bool ibLayoutViewGroup::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyFont->SetNodeValue(node.GetProperty(m_propertyFont->GetName()));
	m_propertyFG->SetNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->SetNodeValue(node.GetProperty(m_propertyBG->GetName()));
	m_propertyTooltip->SetNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyEnabled->SetNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	return true;
}

bool ibLayoutViewGroup::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyFont->GetName(), m_propertyFont->GetNodeValue());
	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	return true;
}
