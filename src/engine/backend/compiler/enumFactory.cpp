////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum factory 
////////////////////////////////////////////////////////////////////////////

#include "enumFactory.h"
#include "backend/propertyManager/propertyManager.h"

//*********************************************************************************************************
//*                                   Singleton class "enumFactory"                                       *
//*********************************************************************************************************

ibValueEnumFactory::ibValueEnumFactory() :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true)
{
	m_members.Bind(this, &ibValueEnumFactory::FillMembers);
}

ibValueEnumFactory::~ibValueEnumFactory() {
}

void ibValueEnumFactory::FillMembers(ibMemberTable& helper) const
{
	for (auto& ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_enum)) {
		helper.AppendProp(ctor->GetClassName());
	}
}

bool ibValueEnumFactory::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const wxString &strEnumeration = GetPropName(lPropNum);
	if (!ibValue::IsRegisterCtor(strEnumeration))
		return false;
	pvarPropVal = ibValue::CreateObject(strEnumeration);
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

CONTEXT_TYPE_REGISTER(ibValueEnumFactory, "EnumManager", context_to_clsid("CO_ENMR"));
