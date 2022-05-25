////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value structure and containers
////////////////////////////////////////////////////////////////////////////

#include "valueMap.h"
#include "methods.h"
#include "functions.h"
#include "utils/stringUtils.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueContainer, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueStructure, CValue);

CMethods CValueContainer::CValueReturnContainer::m_methods;

bool CValueContainer::ContainerComparator::operator() (const CValue& lhs, const CValue& rhs) const
{
	if (lhs.GetType() == eValueTypes::TYPE_STRING
		&& rhs.GetType() == eValueTypes::TYPE_STRING)
	{
		return StringUtils::MakeUpper(lhs.GetString()) < StringUtils::MakeUpper(rhs.GetString());
	}
	else
	{
		return lhs < rhs;
	}
}

//**********************************************************************
//*                          CValueReturnMap                           *
//**********************************************************************

enum
{
	enKey,
	enValue
};

void CValueContainer::CValueReturnContainer::PrepareNames() const
{
	std::vector<SEng> aAttributes =
	{
		{"key"},
		{"value"}
	};

	m_methods.PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void CValueContainer::CValueReturnContainer::SetAttribute(attributeArg_t& aParams, CValue& cValue)
{
}

CValue CValueContainer::CValueReturnContainer::GetAttribute(attributeArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enKey: return m_key;
	case enValue: return m_value;
	}

	return CValue();
}

CMethods CValueContainer::m_methods;

//**********************************************************************
//*                            CValueContainer                         *
//**********************************************************************

CValueContainer::CValueContainer() : CValue(eValueTypes::TYPE_VALUE) {}

CValueContainer::CValueContainer(const std::map<CValue, CValue>& containerValues) : CValue(eValueTypes::TYPE_VALUE, true) { for (auto& cntVal : containerValues) m_containerValues.insert_or_assign(cntVal.first, cntVal.second); }

CValueContainer::CValueContainer(bool readOnly) : CValue(eValueTypes::TYPE_VALUE, readOnly) {}

CValueContainer::~CValueContainer() { m_containerValues.clear(); }

//работа с массивом как с агрегатным объектом
//перечисление строковых ключей
enum
{
	enCount = 0,
	enProperty,

	enClear,
	enDelete,
	enInsert
};

void CValueContainer::PrepareNames() const
{
	std::vector<SEng> aMethods =
	{
		{"count", "count()"},
		{"property", "property(key, valueFound)"}
	};

	if (!m_bReadOnly)
	{
		aMethods.push_back({ "clear", "clear()" });
		aMethods.push_back({ "delete", "delete(key)" });
		aMethods.push_back({ "insert", "insert(key, value)" });
	}

	m_methods.PrepareMethods(aMethods.data(), aMethods.size());

	std::vector<SEng> aAttributes;

	for (auto keyValue : m_containerValues)
	{
		CValue cValKey = keyValue.first;

		if (!cValKey.IsEmpty())
		{
			SEng attributes;
			attributes.sName = cValKey.GetString();
			aAttributes.push_back(attributes);
		}
	}

	m_methods.PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void CValueContainer::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	SetAt(aParams.GetName(), cVal);
}

CValue CValueContainer::GetAttribute(attributeArg_t& aParams)
{
	return GetAt(aParams.GetName());
}

CValue CValueContainer::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enClear: Clear(); break;
	case enCount: return Count();
	case enDelete: Delete(aParams[0]); break;
	case enInsert: Insert(aParams[0], aParams[1]);  break;
	case enProperty: return Property(aParams[0], aParams.GetParamCount() > 1 ? aParams[1] : CValue());
	}

	return CValue();
}

void CValueContainer::Delete(const CValue& cKey)
{
	m_containerValues.erase(cKey);
}

#include "appData.h"

void CValueContainer::Insert(const CValue& cKey, CValue& cValue)
{
	std::map<const CValue, CValue>::iterator itFound = m_containerValues.find(cKey);
	if (itFound != m_containerValues.end()) { if (!appData->DesignerMode()) { CTranslateError::Error(_("Key '" + cKey.GetString() + "' is already using!")); } return; }
	SetAt(cKey, cValue);
}

bool CValueContainer::Property(const CValue& cKey, CValue& cValueFound)
{
	std::map<const CValue, CValue>::iterator itFound = m_containerValues.find(cKey);
	if (itFound != m_containerValues.end()) { cValueFound = itFound->second; return true; }

	return false;
}

CValue CValueContainer::GetItEmpty()
{
	return new CValueReturnContainer();
}

CValue CValueContainer::GetItAt(unsigned int idx)
{
	if (m_containerValues.size() < idx) return CValue();

	auto structurePos = m_containerValues.begin();
	std::advance(structurePos, idx);

	return new CValueReturnContainer(structurePos->first, structurePos->second);
}

void CValueContainer::SetAt(const CValue& cKey, CValue& cVal)
{
	m_containerValues.insert_or_assign(cKey, cVal);
}

CValue CValueContainer::GetAt(const CValue& cKey)
{
	std::map<const CValue, CValue>::iterator itFound = m_containerValues.find(cKey);
	if (itFound != m_containerValues.end()) return itFound->second;

	if (!appData->DesignerMode()) { CTranslateError::Error(_("Key '" + cKey.GetString() + "' not found!")); }
	return CValue();
}

//**********************************************************************
//*                            CValueStructure                         *
//**********************************************************************

#define st_error_conversion _("Error conversion value. Must be string!")

CValue CValueStructure::GetAt(const CValue& cKey)
{
	if (cKey.GetType() != eValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) { 
			CTranslateError::Error(st_error_conversion); 
		} return CValue();
	}

	return CValueContainer::GetAt(cKey);
}

void CValueStructure::SetAt(const CValue& cKey, CValue& cValue)
{
	if (cKey.GetType() != eValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) { 
			CTranslateError::Error(st_error_conversion); 
		} return;
	}

	CValueContainer::SetAt(cKey, cValue);
}

void CValueStructure::Delete(const CValue& cKey)
{
	if (cKey.GetType() != eValueTypes::TYPE_STRING)
	{
		if (!appData->DesignerMode()) {
			CTranslateError::Error(st_error_conversion);
		} return;
	}

	CValueContainer::Delete(cKey);
}

void CValueStructure::Insert(const CValue& cKey, CValue& cValue)
{
	if (cKey.GetType() != eValueTypes::TYPE_STRING)
	{
		if (!appData->DesignerMode()) {
			CTranslateError::Error(st_error_conversion);
		} return;
	}

	CValueContainer::Insert(cKey, cValue);
}

bool CValueStructure::Property(const CValue& cKey, CValue& cValueFound)
{
	if (cKey.GetType() != eValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			CTranslateError::Error(st_error_conversion);
		}
		return false;
	}

	return CValueContainer::Property(cKey, cValueFound);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueContainer, "container", TEXT2CLSID("VL_CONT"));
VALUE_REGISTER(CValueStructure, "structure", TEXT2CLSID("VL_STRU"));

SO_VALUE_REGISTER(CValueContainer::CValueReturnContainer, "keyValue", CValueReturnContainer, TEXT2CLSID("VL_KEVA"));