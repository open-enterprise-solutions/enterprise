////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum factory 
////////////////////////////////////////////////////////////////////////////

#include "enumFactory.h"
#include "backend/wrapper/propertyInfo.h"

//*********************************************************************************************************
//*                                   Singleton class "enumFactory"                                       *
//*********************************************************************************************************

CEnumFactory::CEnumFactory() :
	CValue(eValueTypes::TYPE_VALUE, true), m_methodHelper(new CMethodHelper())
{
	for (auto &ctor : CValue::GetListCtorsByType(eCtorObjectType_object_enum)) {
		m_methodHelper->AppendProp(ctor->GetClassName());
	}
}

CEnumFactory::~CEnumFactory() {
	wxDELETE(m_methodHelper);
}

void CEnumFactory::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	for (auto& ctor : CValue::GetListCtorsByType(eCtorObjectType_object_enum)) {
		m_methodHelper->AppendProp(ctor->GetClassName());
	}
}

bool CEnumFactory::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const wxString &strEnumeration = GetPropName(lPropNum);
	if (!CValue::IsRegisterCtor(strEnumeration))
		return false;
	pvarPropVal = CValue::CreateObject(strEnumeration);
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

CONTEXT_TYPE_REGISTER(CEnumFactory, "enumManager", string_to_clsid("CO_ENMR"));
