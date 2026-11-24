#include "static.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//****************************************************************************
//*                                 Line                                     *
//****************************************************************************

bool ibStaticViewLine::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return true;
}

bool ibStaticViewLine::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return true;
}
