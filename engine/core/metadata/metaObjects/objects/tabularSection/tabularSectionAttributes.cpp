////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - attributes
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
#include "compiler/methods.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

void ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	const meta_identifier_t &id = 
		m_methods->GetAttributePosition(aParams.GetIndex());
	SetValueByMetaID(id, cVal);
}

CValue ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::GetAttribute(attributeArg_t &aParams)
{
	const meta_identifier_t &id = 
		m_methods->GetAttributePosition(aParams.GetIndex());
	return GetValueByMetaID(id);
}