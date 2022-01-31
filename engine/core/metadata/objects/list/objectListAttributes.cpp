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

void CDataObjectList::SetAttribute(attributeArg_t &aParams, CValue &value)        //установка атрибута
{
}

CValue CDataObjectList::GetAttribute(attributeArg_t &aParams)                     //значение атрибута
{
	if (aParams.GetIndex() == enChoiceMode) {
		return m_bChoiceMode;
	}

	return CValue();
}