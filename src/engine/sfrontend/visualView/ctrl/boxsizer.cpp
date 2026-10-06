#include "sizer.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)


//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

ibValueBoxSizer::ibValueBoxSizer() : ibValueSizer()
{
}

//*******************************************************************
//*                            Data									*
//*******************************************************************

bool ibValueBoxSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueSizer::ReadData(node);
}

bool ibValueBoxSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueBoxSizer, "Boxsizer", "Sizer", control_to_clsid("CT_BSZR"));
