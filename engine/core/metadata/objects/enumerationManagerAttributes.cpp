////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration manager - attributes
////////////////////////////////////////////////////////////////////////////

#include "enumerationManager.h"
#include "reference/reference.h"
#include "appData.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************
void CManagerEnumerationValue::SetAttribute(attributeArg_t &aParams, CValue &cValue) {}       //установка атрибута

#include "compiler/methods.h"

CValue CManagerEnumerationValue::GetAttribute(attributeArg_t &aParams)                     //значение атрибута
{
	auto enums = m_metaObject->GetObjectEnums();
	CMetaEnumerationObject *m_enum = enums.at(aParams.GetIndex());

	return new CValueReference(m_metaObject, m_enum->GetGuid());
}