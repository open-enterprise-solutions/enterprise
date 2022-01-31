////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum factory 
////////////////////////////////////////////////////////////////////////////

#include "enumFactory.h"
#include "common/objectbase.h"
#include "methods.h"

static std::map<wxString, CValue *> m_aEnumValues = {};

//*********************************************************************************************************
//*                                   Singleton initializer "enumFactory"                                 *
//*********************************************************************************************************

CEnumFactory *CEnumFactory::s_instance = NULL;

CEnumFactory* CEnumFactory::Get()
{
	if (!s_instance)
	{
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

CEnumFactory::CEnumFactory() : CValue(eValueTypes::TYPE_VALUE, true), m_methods(new CMethods()) {}

CEnumFactory::~CEnumFactory() { delete m_methods; }

void CEnumFactory::AppendEnumeration(const wxString &enumName, CValue *newEnum)
{
	m_aEnumValues.insert_or_assign(enumName, newEnum);
	m_methods->AppendAttribute(enumName); newEnum->IncrRef();
}

CValue *CEnumFactory::GetEnumeration(const wxString &enumName) const
{
	auto itFounded = std::find_if(m_aEnumValues.begin(), m_aEnumValues.end(), [enumName](const std::pair<wxString, CValue> & pair) -> bool { return enumName.CompareTo(pair.first, wxString::caseCompare::ignoreCase) == 0; });
	if (itFounded != m_aEnumValues.end()) return itFounded->second;
	return NULL;
}

void CEnumFactory::RemoveEnumeration(const wxString &enumName)
{
	CValue *enumValue = GetEnumeration(enumName);
	wxASSERT(enumValue);
	m_aEnumValues.erase(enumName); m_methods->RemoveAttribute(enumName); enumValue->DecrRef();
}

CValue CEnumFactory::GetAttribute(attributeArg_t &aParams)
{
	wxString enumeration = aParams.GetName();
	auto itEnumeration = std::find_if(m_aEnumValues.begin(), m_aEnumValues.end(), [enumeration](const std::pair<wxString, CValue> & pair) -> bool { return enumeration.CompareTo(pair.first, wxString::caseCompare::ignoreCase) == 0; });
	if (itEnumeration != m_aEnumValues.end()) return itEnumeration->second;
	return CValue();
}