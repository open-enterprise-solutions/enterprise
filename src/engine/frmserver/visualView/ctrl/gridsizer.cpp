
#include "sizer.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)


//****************************************************************************
//*                             GridSizer                                    *
//****************************************************************************

ibValueGridSizer::ibValueGridSizer() : ibValueSizer()
{
}

void ibValueGridSizer::OnUpdate(ibDataNode& state, ibVisualHost* /*host*/)
{
	state.SetValue(wxT("Visible"), m_propertyVisible->GetValueAsBoolean() && IsAvailable());
}

//**********************************************************************************
//*                           Property                                             *
//**********************************************************************************

bool ibValueGridSizer::ReadData(const ibDataNode& node)
{
	m_propertyRows->SetNodeValue(node.GetProperty(m_propertyRows->GetName()));
	m_propertyCols->SetNodeValue(node.GetProperty(m_propertyCols->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueSizer::ReadData(node);
}

bool ibValueGridSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRows->GetName(), m_propertyRows->GetNodeValue());
	node.SetProperty(m_propertyCols->GetName(), m_propertyCols->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGridSizer, "Gridsizer", "Sizer", control_to_clsid("CT_GSZR"));
