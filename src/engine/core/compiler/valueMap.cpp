////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value structure and containers
////////////////////////////////////////////////////////////////////////////

#include "valueMap.h"
#include "translateError.h"
#include "utils/stringUtils.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueContainer, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueStructure, CValue);

CValue::CMethodHelper CValueContainer::CValueReturnContainer::m_methodHelper;

bool CValueContainer::ContainerComparator::operator() (const CValue& lhs, const CValue& rhs) const {
	if (lhs.GetType() == eValueTypes::TYPE_STRING
		&& rhs.GetType() == eValueTypes::TYPE_STRING) {
		return StringUtils::MakeUpper(lhs.GetString()) < StringUtils::MakeUpper(rhs.GetString());
	}
	else {
		return lhs < rhs;
	}
}

//**********************************************************************
//*                          CValueReturnMap                           *
//**********************************************************************

void CValueContainer::CValueReturnContainer::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendProp(wxT("key"));
	m_methodHelper.AppendProp(wxT("value"));
}

bool CValueContainer::CValueReturnContainer::SetPropVal(const long lPropNum, CValue& cValue)
{
	return false;
}

bool CValueContainer::CValueReturnContainer::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enKey:
		pvarPropVal = m_key;
		return true;
	case enValue:
		pvarPropVal = m_value;
		return true;
	}

	return false;
}

CValue::CMethodHelper CValueContainer::m_methodHelper;

//**********************************************************************
//*                            CValueContainer                         *
//**********************************************************************

CValueContainer::CValueContainer() : CValue(eValueTypes::TYPE_VALUE) {}

CValueContainer::CValueContainer(const std::map<CValue, CValue>& containerValues) : CValue(eValueTypes::TYPE_VALUE, true) {
	for (auto& cntVal : containerValues)
		m_containerValues.insert_or_assign(cntVal.first, cntVal.second);
}

CValueContainer::CValueContainer(bool readOnly) : CValue(eValueTypes::TYPE_VALUE, readOnly) {}

CValueContainer::~CValueContainer() {
	m_containerValues.clear();
}

//работа с массивом как с агрегатным объектом
//перечисление строковых ключей
void CValueContainer::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendFunc(wxT("count"), "count()");
	m_methodHelper.AppendFunc(wxT("property"), 2, "property(key, valueFound)");
	if (!m_bReadOnly) {
		m_methodHelper.AppendFunc(wxT("clear"), "clear()");
		m_methodHelper.AppendFunc(wxT("delete"), 1, "delete(key)");
		m_methodHelper.AppendFunc(wxT("insert"), 2, "insert(key, value)");
	}
	for (auto keyValue : m_containerValues) {
		const CValue& cValKey = keyValue.first;
		if (!cValKey.IsEmpty()) {
			m_methodHelper.AppendProp(cValKey.GetString());
		}
	}
}

bool CValueContainer::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return SetAt(
		GetPropName(lPropNum), varPropVal
	);
}

bool CValueContainer::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	return GetAt(
		GetPropName(lPropNum), pvarPropVal
	);
}

bool CValueContainer::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enClear:
		Clear();
		return true;
	case enCount:
		pvarRetValue = Count();
		return true;
	case enDelete:
		Delete(*paParams[0]);
		return true;
	case enInsert:
		Insert(*paParams[0], *paParams[1]); 		
		return true;
	case enProperty:
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : CValue());
		return true;
	}

	return false;
}

void CValueContainer::Delete(const CValue& varKeyValue)
{
	m_containerValues.erase(varKeyValue);
}

#include "appData.h"

void CValueContainer::Insert(const CValue& varKeyValue, CValue& cValue)
{
	std::map<const CValue, CValue>::iterator itFounded = m_containerValues.find(varKeyValue);
	if (itFounded != m_containerValues.end()) {
		if (!appData->DesignerMode())
			CTranslateError::Error("Key '%s' is already using!", varKeyValue.GetString());
		return;
	}
	SetAt(varKeyValue, cValue);
}

bool CValueContainer::Property(const CValue& varKeyValue, CValue& cValueFound)
{
	std::map<const CValue, CValue>::iterator itFound = m_containerValues.find(varKeyValue);
	if (itFound != m_containerValues.end()) {
		cValueFound = itFound->second;
		return true;
	}
	return false;
}

CValue CValueContainer::GetItEmpty()
{
	return new CValueReturnContainer();
}

CValue CValueContainer::GetItAt(unsigned int idx)
{
	if (m_containerValues.size() < idx)
		return CValue();
	auto structurePos = m_containerValues.begin();
	std::advance(structurePos, idx);
	return new CValueReturnContainer(structurePos->first, structurePos->second);
}

bool CValueContainer::SetAt(const CValue& varKeyValue, const CValue& varValue)
{
	m_containerValues.insert_or_assign(varKeyValue, varValue);
	return true;
}

bool CValueContainer::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	std::map<const CValue, CValue>::const_iterator itFound = m_containerValues.find(varKeyValue);
	if (itFound != m_containerValues.end()) {
		pvarValue = itFound->second; return true;
	}
	if (!appData->DesignerMode())
		CTranslateError::Error("Key '%s' not found!", varKeyValue.GetString());
	return false;
}

//**********************************************************************
//*                            CValueStructure                         *
//**********************************************************************

#define st_error_conversion "Error conversion value. Must be string!"

bool CValueStructure::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	if (varKeyValue.GetType() != eValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) 
			CTranslateError::Error(st_error_conversion);
		return false;
	}
	return CValueContainer::GetAt(varKeyValue, pvarValue);
}

bool CValueStructure::SetAt(const CValue& varKeyValue, const CValue& cValue)
{
	if (varKeyValue.GetType() != eValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			CTranslateError::Error(st_error_conversion);
		} return false;
	}

	return CValueContainer::SetAt(varKeyValue, cValue);
}

void CValueStructure::Delete(const CValue& varKeyValue)
{
	if (varKeyValue.GetType() != eValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			CTranslateError::Error(st_error_conversion);
		} return;
	}

	CValueContainer::Delete(varKeyValue);
}

void CValueStructure::Insert(const CValue& varKeyValue, CValue& cValue)
{
	if (varKeyValue.GetType() != eValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			CTranslateError::Error(st_error_conversion);
		} return;
	}

	CValueContainer::Insert(varKeyValue, cValue);
}

bool CValueStructure::Property(const CValue& varKeyValue, CValue& cValueFound)
{
	if (varKeyValue.GetType() != eValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			CTranslateError::Error(st_error_conversion);
		}
		return false;
	}

	return CValueContainer::Property(varKeyValue, cValueFound);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueContainer, "container", TEXT2CLSID("VL_CONT"));
VALUE_REGISTER(CValueStructure, "structure", TEXT2CLSID("VL_STRU"));

SO_VALUE_REGISTER(CValueContainer::CValueReturnContainer, "keyValue", TEXT2CLSID("VL_KEVA"));