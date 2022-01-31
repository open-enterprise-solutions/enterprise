////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - attributes
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
#include "compiler/methods.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

void IValueTabularSection::CValueTabularSectionReturnLine::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	meta_identifier_t id = 
		m_methods->GetAttributePosition(aParams.GetIndex());
	SetValueByMetaID(id, cVal);
}

CValue IValueTabularSection::CValueTabularSectionReturnLine::GetAttribute(attributeArg_t &aParams)
{
	meta_identifier_t id = 
		m_methods->GetAttributePosition(aParams.GetIndex());
	return GetValueByMetaID(id);
}