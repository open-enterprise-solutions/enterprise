////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value structure and containers
////////////////////////////////////////////////////////////////////////////

#include "valueMap.h"
#include "backend/backend_exception.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueContainer, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueStructure, ibValue);

ibValue::ibValueMethodHelper ibValueContainer::ibValueReturnContainer::m_methodHelper;

bool ibValueContainer::ContainerComparator::operator() (const ibValue& lhs, const ibValue& rhs) const {
	if (lhs.GetType() == ibValueTypes::TYPE_STRING
		&& rhs.GetType() == ibValueTypes::TYPE_STRING) {
		return stringUtils::MakeUpper(lhs.GetString()) < stringUtils::MakeUpper(rhs.GetString());
	}
	else {
		return lhs < rhs;
	}
}

//**********************************************************************
//*                          ibValueReturnMap                           *
//**********************************************************************

void ibValueContainer::ibValueReturnContainer::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendProp(wxT("Key"));
	m_methodHelper.AppendProp(wxT("Value"));
}

bool ibValueContainer::ibValueReturnContainer::SetPropVal(const long lPropNum, ibValue& cValue)
{
	return false;
}

bool ibValueContainer::ibValueReturnContainer::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
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

//**********************************************************************
//*                            ibValueContainer                         *
//**********************************************************************

ibValueContainer::ibValueContainer() : ibValue(ibValueTypes::TYPE_VALUE), m_methodHelper(new ibValueMethodHelper) {}

ibValueContainer::ibValueContainer(const std::map<ibValue, ibValue>& containerValues) : ibValue(ibValueTypes::TYPE_VALUE, true), m_methodHelper(new ibValueMethodHelper) {
	for (auto& cntVal : containerValues) {
		m_containerValues.insert_or_assign(cntVal.first, cntVal.second);
	}
	PrepareNames();
}

ibValueContainer::ibValueContainer(bool readOnly) : ibValue(ibValueTypes::TYPE_VALUE, readOnly), m_methodHelper(new ibValueMethodHelper) {
}

ibValueContainer::~ibValueContainer() {
	m_containerValues.clear();
	wxDELETE(m_methodHelper);
}

//������ � �������� ��� � ���������� ��������
//������������ ��������� ������
void ibValueContainer::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc(wxT("Count"), wxT("Count()"));
	m_methodHelper->AppendFunc(wxT("Property"), 2, wxT("Property(key : any, valueFound : any)"));

	if (!m_bReadOnly) {
		m_methodHelper->AppendFunc(wxT("Clear"), wxT("Clear()"));
		m_methodHelper->AppendFunc(wxT("Delete"), 1, wxT("Delete(key : any)"));
		m_methodHelper->AppendFunc(wxT("Insert"), 2, wxT("Insert(key : any, value : any)"));
	}

	for (auto keyValue : m_containerValues) {
		const ibValue& cValKey = keyValue.first;
		if (!cValKey.IsEmpty()) {
			m_methodHelper->AppendProp(cValKey.GetString());
		}
	}
}

bool ibValueContainer::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return SetAt(
		GetPropName(lPropNum), varPropVal
	);
}

bool ibValueContainer::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return GetAt(
		GetPropName(lPropNum), pvarPropVal
	);
}

bool ibValueContainer::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
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
	{
		ibValue defaultVal;
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : defaultVal);
	}
		return true;
	}

	return false;
}

void ibValueContainer::Delete(const ibValue& varKeyValue)
{
	if (m_methodHelper != nullptr) m_methodHelper->RemoveProp(varKeyValue.GetString());
	m_containerValues.erase(varKeyValue);
}

#include "backend/appData.h"

void ibValueContainer::Insert(const ibValue& varKeyValue, const ibValue& cValue)
{
	std::map<const ibValue, ibValue>::iterator it = m_containerValues.find(varKeyValue);
	if (it != m_containerValues.end()) {
		if (!appData->DesignerMode())
			ibBackendCoreException::Error(_("Key '%s' is already using!"), varKeyValue.GetString());
		return;
	}
	if (m_methodHelper != nullptr) m_methodHelper->AppendProp(varKeyValue.GetString());
	m_containerValues.insert_or_assign(varKeyValue, cValue);
}

bool ibValueContainer::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	std::map<const ibValue, ibValue>::iterator itFound = m_containerValues.find(varKeyValue);
	if (itFound != m_containerValues.end()) {
		cValueFound = itFound->second;
		return true;
	}
	return false;
}

ibValue ibValueContainer::GetIteratorEmpty()
{
	return ibValue::CreateAndPrepareValueRef<ibValueReturnContainer>();
}

ibValue ibValueContainer::GetIteratorAt(unsigned int idx)
{
	if (m_containerValues.size() < idx)
		return ibValue();
	auto structurePos = m_containerValues.begin();
	std::advance(structurePos, idx);
	return ibValue::CreateAndPrepareValueRef<ibValueReturnContainer>(structurePos->first, structurePos->second);
}

bool ibValueContainer::SetAt(const ibValue& varKeyValue, const ibValue& varValue)
{
	ibValueContainer::Insert(varKeyValue, varValue);
	return true;
}

bool ibValueContainer::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	std::map<const ibValue, ibValue>::const_iterator itFound = m_containerValues.find(varKeyValue);
	if (itFound != m_containerValues.end()) {
		pvarValue = itFound->second; return true;
	}
	if (!appData->DesignerMode())
		ibBackendCoreException::Error(_("Key '%s' not found!"), varKeyValue.GetString());
	return false;
}

//**********************************************************************
//*                            ibValueStructure                         *
//**********************************************************************

#define st_error_conversion _("Error conversion value. Must be string!")

bool ibValueStructure::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode())
			ibBackendCoreException::Error(st_error_conversion);
		return false;
	}
	return ibValueContainer::GetAt(varKeyValue, pvarValue);
}

bool ibValueStructure::SetAt(const ibValue& varKeyValue, const ibValue& cValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return false;
	}

	return ibValueContainer::SetAt(varKeyValue, cValue);
}

void ibValueStructure::Delete(const ibValue& varKeyValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return;
	}

	ibValueContainer::Delete(varKeyValue);
}

void ibValueStructure::Insert(const ibValue& varKeyValue, const ibValue& cValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return;
	}

	ibValueContainer::Insert(varKeyValue, cValue);
}

bool ibValueStructure::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		}
		return false;
	}

	return ibValueContainer::Property(varKeyValue, cValueFound);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueContainer, "Container", string_to_clsid("VL_CONTR"));
VALUE_TYPE_REGISTER(ibValueStructure, "Structure", string_to_clsid("VL_STRUT"));

SYSTEM_TYPE_REGISTER(ibValueContainer::ibValueReturnContainer, "KeyValue", string_to_clsid("VL_KEVAL"));