#include "sizer.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)


//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

ibValueBoxSizer::ibValueBoxSizer() : ibValueSizer()
{
}

void ibValueBoxSizer::OnUpdate(ibDataNode& state, ibVisualHost* /*host*/)
{
	state.SetValue(wxT("Visible"), m_propertyVisible->GetValueAsBoolean() && IsAvailable());
}

//*******************************************************************
//*                            Data									*
//*******************************************************************

bool ibValueBoxSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	return ibValueSizer::ReadData(node);
}

bool ibValueBoxSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueBoxSizer, "Boxsizer", "Sizer", control_to_clsid("CT_BSZR"));
