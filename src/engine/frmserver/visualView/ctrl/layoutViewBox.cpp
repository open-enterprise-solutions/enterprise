#include "layout.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//****************************************************************************
//*                                 Box                                      *
//****************************************************************************

bool ibLayoutViewBox::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return true;
}

bool ibLayoutViewBox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return true;
}
