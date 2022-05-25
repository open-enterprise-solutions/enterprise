////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : information register manager manager - attributes
////////////////////////////////////////////////////////////////////////////

#include "informationRegisterManager.h"
#include "appData.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************
void CInformationRegisterManager::SetAttribute(attributeArg_t& aParams, CValue& cValue) {}       //установка атрибута

#include "compiler/methods.h"

CValue CInformationRegisterManager::GetAttribute(attributeArg_t& aParams)                     //значение атрибута
{
	return CValue();
}