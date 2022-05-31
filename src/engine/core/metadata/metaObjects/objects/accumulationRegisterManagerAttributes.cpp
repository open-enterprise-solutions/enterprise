////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumulation register manager manager - attributes
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegisterManager.h"
#include "appData.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************
void CAccumulationRegisterManager::SetAttribute(attributeArg_t& aParams, CValue& cValue) {}       //установка атрибута

#include "compiler/methods.h"

CValue CAccumulationRegisterManager::GetAttribute(attributeArg_t& aParams)                     //значение атрибута
{
	return CValue();
}