#include "static.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//****************************************************************************
//*                                 Text                                     *
//****************************************************************************

void ibStaticViewText::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// The caption is the whole control. Several lines ride on the line breaks in the text; no word-wrap at a width.
	state.SetValue(ibProtocol::ibForm::ibState::Caption, GetControlTitle());
}

bool ibStaticViewText::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	return true;
}

bool ibStaticViewText::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	return true;
}
