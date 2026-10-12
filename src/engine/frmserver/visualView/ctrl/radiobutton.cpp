
#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#include "form.h"
#include "backend/metaData.h"


//****************************************************************************
//*                             Radiobutton                                  *
//****************************************************************************

ibValueRadioButton::ibValueRadioButton() : ibValueWindow(), ibTypeControlFactory()
{
}

ibSourceObject* ibValueRadioButton::GetSourceObject() const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceObject() : nullptr;
}

bool ibValueRadioButton::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceList(GetFilterSourceDataType(), out) : false;
}

const ibMetaData* ibValueRadioButton::GetMetaData() const
{
	return m_formOwner != nullptr ? m_formOwner->GetMetaData() : nullptr;
}

void ibValueRadioButton::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);
	state.SetValue(wxT("Caption"), GetCaption());
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool ibValueRadioButton::ReadData(const ibDataNode& node)
{
	const bool ok = ibValueWindow::ReadData(node);
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));
	m_propertyChoiceValue->SetNodeValue(node.GetProperty(m_propertyChoiceValue->GetName()));
	return ok;
}

bool ibValueRadioButton::WriteData(ibDataNode& node) const
{
	const bool ok = ibValueWindow::WriteData(node);
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());
	node.SetProperty(m_propertyChoiceValue->GetName(), m_propertyChoiceValue->GetNodeValue());
	return ok;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueRadioButton, "Radiobutton", "Widget", control_to_clsid("CT_RDBT"));
