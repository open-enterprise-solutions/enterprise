#include "layout.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//****************************************************************************
//*                                 Grid                                     *
//****************************************************************************

bool ibLayoutViewGrid::ReadData(const ibDataNode& node)
{
	m_propertyRows->SetNodeValue(node.GetProperty(m_propertyRows->GetName()));
	m_propertyCols->SetNodeValue(node.GetProperty(m_propertyCols->GetName()));
	return true;
}

bool ibLayoutViewGrid::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRows->GetName(), m_propertyRows->GetNodeValue());
	node.SetProperty(m_propertyCols->GetName(), m_propertyCols->GetNodeValue());
	return true;
}
