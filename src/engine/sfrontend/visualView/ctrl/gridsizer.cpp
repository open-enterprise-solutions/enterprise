
#include "sizer.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)


//****************************************************************************
//*                             GridSizer                                    *
//****************************************************************************

ibValueGridSizer::ibValueGridSizer() : ibValueSizer()
{
}

//**********************************************************************************
//*                           Property                                             *
//**********************************************************************************

bool ibValueGridSizer::ReadData(const ibDataNode& node)
{
	m_propertyRows->SetNodeValue(node.GetProperty(m_propertyRows->GetName()));
	m_propertyCols->SetNodeValue(node.GetProperty(m_propertyCols->GetName()));

	return ibValueSizer::ReadData(node);
}

bool ibValueGridSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRows->GetName(), m_propertyRows->GetNodeValue());
	node.SetProperty(m_propertyCols->GetName(), m_propertyCols->GetNodeValue());

	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGridSizer, "Gridsizer", "Sizer", control_to_clsid("CT_GSZR"));
