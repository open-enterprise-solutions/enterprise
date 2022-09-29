////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum factory 
////////////////////////////////////////////////////////////////////////////

#include "enumFactory.h"
#include "common/propertyObject.h"
#include "methods.h"

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
	CValue(eValueTypes::TYPE_VALUE, true), m_methods(new CMethods())
{
	for (auto enumeration : CValue::GetAvailableObjects(eObjectType_enum)) {
		m_methods->AppendAttribute(enumeration);
	}
}

CEnumFactory::~CEnumFactory() {
	wxDELETE(m_methods);
}

void CEnumFactory::PrepareNames() const
{
	for (auto enumeration : CValue::GetAvailableObjects(eObjectType_enum)) {
		m_methods->AppendAttribute(enumeration);
	}
}

CValue CEnumFactory::GetAttribute(attributeArg_t& aParams)
{
	wxString enumeration = aParams.GetName();

	if (!CValue::IsRegisterObject(enumeration))
		return CValue();

	return CValue::CreateObject(enumeration);;
}