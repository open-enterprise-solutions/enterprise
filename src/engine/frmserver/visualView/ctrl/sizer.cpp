#include "sizer.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueSizer::ReadData(const ibDataNode& node)
{
	m_propertyMinSize->SetNodeValue(node.GetProperty(m_propertyMinSize->GetName()));

	return ibValueControl::ReadData(node);
}

bool ibValueSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMinSize->GetName(), m_propertyMinSize->GetNodeValue());

	return ibValueControl::WriteData(node);
}