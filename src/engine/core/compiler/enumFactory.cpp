////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum factory 
////////////////////////////////////////////////////////////////////////////

#include "enumFactory.h"
#include "core/common/propertyInfo.h"

//*********************************************************************************************************
//*                                   Singleton initializer "enumFactory"                                 *
//*********************************************************************************************************

CEnumFactory* CEnumFactory::s_instance = NULL;

CEnumFactory* CEnumFactory::Get()
{
	if (s_instance == NULL) {
		s_instance = new CEnumFactory();
		s_instance->IncrRef();
	}

	return s_instance;
}

void CEnumFactory::Destroy()
{
	wxDELETE(s_instance);
}

//*********************************************************************************************************
//*                                   Singleton class "enumFactory"                                       *
//*********************************************************************************************************

CEnumFactory::CEnumFactory() :
	CValue(eValueTypes::TYPE_VALUE, true), m_methodHelper(new CMethodHelper())
{
	for (auto enumeration : CValue::GetAvailableObjects(eObjectType_enum)) {
		m_methodHelper->AppendProp(enumeration);
	}
}

CEnumFactory::~CEnumFactory() {
	wxDELETE(m_methodHelper);
}

void CEnumFactory::PrepareNames() const
{
	for (auto enumeration : CValue::GetAvailableObjects(eObjectType_enum)) {
		m_methodHelper->AppendProp(enumeration);
	}
}

bool CEnumFactory::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const wxString &enumeration = GetPropName(lPropNum);
	if (!CValue::IsRegisterObject(enumeration))
		return false;
	pvarPropVal = CValue::CreateObject(enumeration);
	return true;
}