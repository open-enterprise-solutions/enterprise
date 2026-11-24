#include "frame.h"   // ibValueFrame — the control it answers as; sizerItem.h with it
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//**********************************************************************************
//*                          the place, as its control                             *
//**********************************************************************************

ibString ibSizerItem::GetClassName() const
{
	return m_control->GetClassName();
}

ibString ibSizerItem::GetObjectTypeName() const
{
	return m_control->GetObjectTypeName();
}

bool ibSizerItem::IsEditable() const
{
	return m_control->IsEditable();
}

const ibMetaData* ibSizerItem::GetMetaData() const
{
	return m_control->GetMetaData();
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibSizerItem::ReadData(const ibDataNode& node)
{
	m_propertyProportion->SetNodeValue(node.GetProperty(m_propertyProportion->GetName()));

	m_propertyFlagBorderLeft->SetNodeValue(node.GetProperty(m_propertyFlagBorderLeft->GetName()));
	m_propertyFlagBorderRight->SetNodeValue(node.GetProperty(m_propertyFlagBorderRight->GetName()));
	m_propertyFlagBorderTop->SetNodeValue(node.GetProperty(m_propertyFlagBorderTop->GetName()));
	m_propertyFlagBorderBottom->SetNodeValue(node.GetProperty(m_propertyFlagBorderBottom->GetName()));

	m_propertyFlagState->SetNodeValue(node.GetProperty(m_propertyFlagState->GetName()));
	m_propertyBorder->SetNodeValue(node.GetProperty(m_propertyBorder->GetName()));

	return true;
}

bool ibSizerItem::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyProportion->GetName(), m_propertyProportion->GetNodeValue());

	node.SetProperty(m_propertyFlagBorderLeft->GetName(), m_propertyFlagBorderLeft->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderRight->GetName(), m_propertyFlagBorderRight->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderTop->GetName(), m_propertyFlagBorderTop->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderBottom->GetName(), m_propertyFlagBorderBottom->GetNodeValue());

	node.SetProperty(m_propertyFlagState->GetName(), m_propertyFlagState->GetNodeValue());
	node.SetProperty(m_propertyBorder->GetName(), m_propertyBorder->GetNodeValue());

	return true;
}
