////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : type unit
////////////////////////////////////////////////////////////////////////////

#include "valueType.h"
#include "valueArray.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueType, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierNumber, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierDate, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierString, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueTypeDescription, CValue);

//////////////////////////////////////////////////////////////////////

CValueType::CValueType(const CLASS_ID& clsid) :
	CValue(eValueTypes::TYPE_VALUE, true)
{
	m_clsid = clsid;
}

#include "core/metadata/metadata.h"

CValueType::CValueType(const wxString& typeName) :
	CValue(eValueTypes::TYPE_VALUE, true)
{
	m_clsid = metadata->GetIDObjectFromString(typeName);
}

CValueType::CValueType(const CValue& cObject) :
	CValue(eValueTypes::TYPE_VALUE, true)
{
	m_clsid = cObject.GetTypeClass();
}

CValueType::CValueType(const CValueType& cType) :
	CValue(eValueTypes::TYPE_VALUE, true)
{
	m_clsid = cType.m_clsid;
}

wxString CValueType::GetString() const
{
	return metadata->GetNameObjectFromID(m_clsid);
}

//////////////////////////////////////////////////////////////////////

#include "core/compiler/systemObjects.h"

CValue CValueTypeDescription::AdjustValue(const typeDescription_t& typeDescription)
{
	if (!typeDescription.IsOk())
		return CValue();

	if (1 == typeDescription.m_clsids.size()) {
		auto posIt = typeDescription.m_clsids.begin();
		std::advance(posIt, 0);
		return metadata->CreateObject(*posIt);
	}

	return CValue();
}

CValue CValueTypeDescription::AdjustValue(const typeDescription_t& typeDescription, const CValue& varValue)
{
	if (!typeDescription.IsOk())
		return varValue;

	auto foundedIt = std::find(typeDescription.m_clsids.begin(), typeDescription.m_clsids.end(), varValue.GetTypeClass());
	if (foundedIt != typeDescription.m_clsids.end()) {
		eValueTypes vt = CValue::GetVTByID(varValue.GetTypeClass());
		if (vt < eValueTypes::TYPE_REFFER) {
			if (vt == eValueTypes::TYPE_NUMBER) {
				return CSystemObjects::Round(varValue, typeDescription.m_typeData.m_number.m_scale);
			}
			else if (vt == eValueTypes::TYPE_DATE) {
				if (typeDescription.m_typeData.m_date.m_dateTime == eDateFractions::eDateFractions_Date)
					return CSystemObjects::BegOfDay(varValue);
				else if (typeDescription.m_typeData.m_date.m_dateTime == eDateFractions::eDateFractions_Time)
					return varValue;
				return varValue;
			}
			else if (vt == eValueTypes::TYPE_STRING) {
				return CSystemObjects::Left(varValue, typeDescription.m_typeData.m_string.m_length);
			}
		}
		return varValue;
	}

	if (typeDescription.m_clsids.size() == 1) {
		auto posIt = typeDescription.m_clsids.begin();
		std::advance(posIt, 0);
		if (metadata->IsRegisterObject(*posIt)) {
			eValueTypes vt = CValue::GetVTByID(*posIt);
			if (vt < eValueTypes::TYPE_REFFER) {
				if (vt == eValueTypes::TYPE_NUMBER) {
					return CSystemObjects::Round(varValue, typeDescription.m_typeData.m_number.m_scale);
				}
				else if (vt == eValueTypes::TYPE_DATE) {
					if (typeDescription.m_typeData.m_date.m_dateTime == eDateFractions::eDateFractions_Date)
						return CSystemObjects::BegOfDay(varValue);
					else if (typeDescription.m_typeData.m_date.m_dateTime == eDateFractions::eDateFractions_Time)
						return varValue;
					return varValue.GetDate();
				}
				else if (vt == eValueTypes::TYPE_STRING) {
					return CSystemObjects::Left(varValue, typeDescription.m_typeData.m_string.m_length);

				}
			}
			return metadata->CreateObject(*posIt);
		}
	}
	return CValue();
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CValueTypeDescription::CValueTypeDescription() :
	CValue(eValueTypes::TYPE_VALUE, true), m_methodHelper(new CMethodHelper())
{
}

CValueTypeDescription::CValueTypeDescription(CValueType* valueType) :
	CValue(eValueTypes::TYPE_VALUE, true), m_typeDescription({ valueType ? valueType->GetOwnerTypeClass() : NULL }), m_methodHelper(new CMethodHelper())
{
}

CValueTypeDescription::CValueTypeDescription(CValueType* valueType, CValueQualifierNumber* qNumber, CValueQualifierDate* qDate, CValueQualifierString* qString) :
	CValue(eValueTypes::TYPE_VALUE, true), m_typeDescription({ valueType ? valueType->GetOwnerTypeClass() : NULL }, (qNumber ? *qNumber : qualifierNumber_t()), (qDate ? *qDate : qualifierDate_t()), (qString ? *qString : qualifierString_t())), m_methodHelper(new CMethodHelper())
{
}

CValueTypeDescription::CValueTypeDescription(const typeDescription_t& typeDescription)
	: CValue(eValueTypes::TYPE_VALUE, true), m_typeDescription(typeDescription), m_methodHelper(new CMethodHelper())
{
}

CValueTypeDescription::CValueTypeDescription(const std::set<CLASS_ID>& clsid) :
	CValue(eValueTypes::TYPE_VALUE, true), m_typeDescription(clsid), m_methodHelper(new CMethodHelper())
{
}

CValueTypeDescription::CValueTypeDescription(const std::set<CLASS_ID>& clsids, CValueQualifierNumber* qNumber, CValueQualifierDate* qDate, CValueQualifierString* qString) :
	CValue(eValueTypes::TYPE_VALUE, true), m_typeDescription(clsids, (qNumber ? *qNumber : qualifierNumber_t()), (qDate ? *qDate : qualifierDate_t()), (qString ? *qString : qualifierString_t())), m_methodHelper(new CMethodHelper())
{
}

CValueTypeDescription::~CValueTypeDescription()
{
	wxDELETE(m_methodHelper);
}

#include "core/metadata/metadata.h"

bool CValueTypeDescription::Init(CValue** paParams, const long lSizeArray)
{
	if (paParams[0]->GetType() == eValueTypes::TYPE_STRING) {
		wxString classType = paParams[0]->GetString();
		if (metadata->IsRegisterObject(classType)) {
			CValueQualifierNumber* qNumber = NULL;
			if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
				m_typeDescription.m_typeData.m_number = *qNumber;
			CValueQualifierDate* qDate = NULL;
			if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
				m_typeDescription.m_typeData.m_date = *qDate;
			CValueQualifierString* qString = NULL;
			if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
				m_typeDescription.m_typeData.m_string = *qString;
			m_typeDescription.m_clsids.insert(
				metadata->GetIDObjectFromString(classType)
			);
			return true;
		}
	}

	CValueArray* valArray = NULL;
	if (paParams[0]->ConvertToValue(valArray)) {
		CValueQualifierNumber* qNumber = NULL;
		if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
			m_typeDescription.m_typeData.m_number = *qNumber;
		CValueQualifierDate* qDate = NULL;
		if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
			m_typeDescription.m_typeData.m_date = *qDate;
		CValueQualifierString* qString = NULL;
		if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
			m_typeDescription.m_typeData.m_string = *qString;
		for (unsigned int i = 0; i < valArray->Count(); i++) {
			CValue retValue; valArray->GetAt(i, retValue);
			CValueType* valType = value_cast<CValueType>(retValue);
			wxASSERT(valType);
			m_typeDescription.m_clsids.insert(
				valType->GetOwnerTypeClass()
			);
		}
		/*std::sort(m_clsids.begin(), m_clsids.end(), [](const CLASS_ID& a, const CLASS_ID& b) {
			return a < b; }
		);*/
		return true;
	}

	CValueType* valType = NULL;
	if (paParams[0]->ConvertToValue(valType)) {
		CValueQualifierNumber* qNumber = NULL;
		if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
			m_typeDescription.m_typeData.m_number = *qNumber;
		CValueQualifierDate* qDate = NULL;
		if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
			m_typeDescription.m_typeData.m_date = *qDate;
		CValueQualifierString* qString = NULL;
		if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
			m_typeDescription.m_typeData.m_string = *qString;
		m_typeDescription.m_clsids.insert(
			valType->GetOwnerTypeClass()
		);
		return true;
	}

	return false;
}

bool CValueTypeDescription::ContainType(const CValue& cType) const
{
	CValueType* valueType = value_cast<CValueType>(cType);
	wxASSERT(valueType);
	auto itFounded = std::find(m_typeDescription.m_clsids.begin(), m_typeDescription.m_clsids.begin(), valueType->GetOwnerTypeClass());
	return itFounded != m_typeDescription.m_clsids.end();
}

CValue CValueTypeDescription::AdjustValue() const
{
	return AdjustValue(m_typeDescription);
}

CValue CValueTypeDescription::AdjustValue(const CValue& varValue) const
{
	return AdjustValue(m_typeDescription, varValue);
}

CValue CValueTypeDescription::Types() const
{
	CValueArray* arr = new CValueArray;
	for (auto clsid : m_typeDescription.m_clsids)
		arr->Add(new CValueType(clsid));
	return arr;
}

enum Func {
	enContainsType = 0,
	enAdjustValue,
	enTypes
};

void CValueTypeDescription::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendConstructor(4, "typeDescription(type, qNumber, qDate, qString)");

	m_methodHelper->AppendFunc("containType", 1, "containsType(type)");
	m_methodHelper->AppendFunc("adjustValue", 1, "adjustValue(value = undefined");
	m_methodHelper->AppendFunc("types", "types()");
}

bool CValueTypeDescription::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enContainsType:
		pvarRetValue = ContainType(*paParams[0]);
		return true;
	case enAdjustValue: {
		if (lSizeArray > 0)
			pvarRetValue = AdjustValue(*paParams[0]);
		pvarRetValue = AdjustValue();
		return true;
	}
	case enTypes:
		pvarRetValue = Types();
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueType, "type", TEXT2CLSID("VL_TYPE"));
VALUE_REGISTER(CValueTypeDescription, "typeDescription", TEXT2CLSID("VL_TYDE"));

VALUE_REGISTER(CValueQualifierNumber, "qualifierNumber", TEXT2CLSID("VL_QLFN"));
VALUE_REGISTER(CValueQualifierDate, "qualifierDate", TEXT2CLSID("VL_QLFD"));
VALUE_REGISTER(CValueQualifierString, "qualifierString", TEXT2CLSID("VL_QLFS"));