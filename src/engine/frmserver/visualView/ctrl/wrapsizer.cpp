
#include "sizer.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)


//****************************************************************************
//*                             WrapSizer                                    *
//****************************************************************************

ibValueWrapSizer::ibValueWrapSizer() : ibValueSizer()
{
}

//**********************************************************************************
//*                            Data												   *
//**********************************************************************************

bool ibValueWrapSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueSizer::ReadData(node);
}

bool ibValueWrapSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueWrapSizer, "Wrapsizer", "Sizer");