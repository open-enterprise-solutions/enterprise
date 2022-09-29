////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list attrubutes 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

enum {
	enChoiceMode
};

void CListDataObjectRef::SetAttribute(attributeArg_t& aParams, CValue& value) //установка атрибута
{
}

CValue CListDataObjectRef::GetAttribute(attributeArg_t& aParams) //значение атрибута
{
	if (aParams.GetIndex() == enChoiceMode) {
		return m_choiceMode;
	}

	return CValue();
}

void CListDataObjectGroupRef::SetAttribute(attributeArg_t& aParams, CValue& value) //установка атрибута
{
}

CValue CListDataObjectGroupRef::GetAttribute(attributeArg_t& aParams) //значение атрибута
{
	if (aParams.GetIndex() == enChoiceMode) {
		return m_choiceMode;
	}

	return CValue();
}

void CListRegisterObject::SetAttribute(attributeArg_t& aParams, CValue& value) //установка атрибута
{
}

CValue CListRegisterObject::GetAttribute(attributeArg_t& aParams) //значение атрибута
{
	return CValue();
}