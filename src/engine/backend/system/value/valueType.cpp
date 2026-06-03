////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : type unit
////////////////////////////////////////////////////////////////////////////

#include "valueType.h"
#include "valueArray.h"

//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////

ibValueType::ibValueType(const ibClassID& clsid) :
	ibValue(ibValueTypes::TYPE_VALUE, true)
{
	m_clsid = clsid;
}

#include "backend/metadataConfiguration.h"

ibValueType::ibValueType(const wxString& typeName) :
	ibValue(ibValueTypes::TYPE_VALUE, true)
{
	m_clsid = activeMetaData->GetIDObjectFromString(typeName);
}

ibValueType::ibValueType(const ibValue& cObject) :
	ibValue(ibValueTypes::TYPE_VALUE, true)
{
	m_clsid = cObject.GetClassType();
}

ibValueType::ibValueType(const ibValueType& cType) :
	ibValue(ibValueTypes::TYPE_VALUE, true)
{
	m_clsid = cType.m_clsid;
}

wxString ibValueType::GetString() const
{
	return activeMetaData->GetNameObjectFromID(m_clsid);
}

//////////////////////////////////////////////////////////////////////

#include "backend/system/systemManager.h"

ibValue ibValueTypeDescription::AdjustValue(const ibTypeDescription& typeDescription,
	const ibMetaData* metaData)
{
	if (!typeDescription.IsOk())
		return wxEmptyValue;

	if (typeDescription.GetClsidCount() == 1) {

		if (metaData != nullptr) {
			return metaData->CreateObject(
				typeDescription.GetFirstClsid()
			);
		}

		return activeMetaData->CreateObject(
			typeDescription.GetFirstClsid()
		);
	}

	return wxEmptyValue;
}

ibValue ibValueTypeDescription::AdjustValue(const ibTypeDescription& typeDescription, const ibValue& varValue,
	const ibMetaData* metaData)
{
	if (!typeDescription.IsOk())
		return varValue;

	auto iterator = std::find(typeDescription.m_listTypeClass.begin(), typeDescription.m_listTypeClass.end(), varValue.GetClassType());
	if (iterator != typeDescription.m_listTypeClass.end()) {
		ibValueTypes vt = ibValue::GetVTByID(varValue.GetClassType());
		if (vt < ibValueTypes::TYPE_REFFER) {
			if (vt == ibValueTypes::TYPE_NUMBER) {
				return ibValueSystemFunction::Round(varValue, typeDescription.m_typeData.m_number.m_scale);
			}
			else if (vt == ibValueTypes::TYPE_DATE) {
				if (typeDescription.m_typeData.m_date.m_dateTime == ibDateFractions::ibDateFractions_Date)
					return ibValueSystemFunction::BegOfDay(varValue);
				else if (typeDescription.m_typeData.m_date.m_dateTime == ibDateFractions::ibDateFractions_Time)
					return varValue;
				return varValue;
			}
			else if (vt == ibValueTypes::TYPE_STRING) {
				return ibValueSystemFunction::Left(varValue, typeDescription.m_typeData.m_string.m_length);
			}
		}
		return varValue;
	}

	if (typeDescription.GetClsidCount() == 1) {

		if (metaData != nullptr ? metaData->IsRegisterCtor(typeDescription.GetFirstClsid()) : activeMetaData->IsRegisterCtor(typeDescription.GetFirstClsid())) {

			ibValueTypes vt = ibValue::GetVTByID(typeDescription.GetFirstClsid());
			if (vt < ibValueTypes::TYPE_REFFER) {
				if (vt == ibValueTypes::TYPE_NUMBER) {
					return ibValueSystemFunction::Round(varValue, typeDescription.m_typeData.m_number.m_scale);
				}
				else if (vt == ibValueTypes::TYPE_DATE) {
					if (typeDescription.m_typeData.m_date.m_dateTime == ibDateFractions::ibDateFractions_Date)
						return ibValueSystemFunction::BegOfDay(varValue);
					else if (typeDescription.m_typeData.m_date.m_dateTime == ibDateFractions::ibDateFractions_Time)
						return varValue;
					return varValue.GetDate();
				}
				else if (vt == ibValueTypes::TYPE_STRING) {
					return ibValueSystemFunction::Left(varValue, typeDescription.m_typeData.m_string.m_length);
				}
			}

			if (metaData != nullptr)
				return metaData->CreateObject(
					typeDescription.GetFirstClsid()
				);

			return activeMetaData->CreateObject(
				typeDescription.GetFirstClsid()
			);
		}
	}
	return wxEmptyValue;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

ibValueTypeDescription::ibValueTypeDescription() :
	ibValue(ibValueTypes::TYPE_VALUE, true), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueTypeDescription::ibValueTypeDescription(ibValueType* valueType) :
	ibValue(ibValueTypes::TYPE_VALUE, true), m_typeDesc({ valueType ? valueType->GetOwnerTypeClass() : 0 }), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueTypeDescription::ibValueTypeDescription(ibValueType* valueType, ibValueQualifierNumber* qNumber, ibValueQualifierDate* qDate, ibValueQualifierString* qString) :
	ibValue(ibValueTypes::TYPE_VALUE, true), m_typeDesc({ valueType ? valueType->GetOwnerTypeClass() : 0 }, (qNumber ? *qNumber : ibQualifierNumber()), (qDate ? *qDate : ibQualifierDate()), (qString ? *qString : ibQualifierString())), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueTypeDescription::ibValueTypeDescription(const ibTypeDescription& typeDescription)
	: ibValue(ibValueTypes::TYPE_VALUE, true), m_typeDesc(typeDescription), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueTypeDescription::ibValueTypeDescription(const std::vector<ibClassID>& array) :
	ibValue(ibValueTypes::TYPE_VALUE, true), m_typeDesc(array), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueTypeDescription::ibValueTypeDescription(const std::vector<ibClassID>& array, ibValueQualifierNumber* qNumber, ibValueQualifierDate* qDate, ibValueQualifierString* qString) :
	ibValue(ibValueTypes::TYPE_VALUE, true), m_typeDesc(array, (qNumber ? *qNumber : ibQualifierNumber()), (qDate ? *qDate : ibQualifierDate()), (qString ? *qString : ibQualifierString())), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueTypeDescription::~ibValueTypeDescription()
{
	wxDELETE(m_methodHelper);
}

bool ibValueTypeDescription::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		wxString classType = paParams[0]->GetString();
		if (activeMetaData->IsRegisterCtor(classType)) {
			ibValueQualifierNumber* qNumber = nullptr;
			if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
				m_typeDesc.m_typeData.m_number = *qNumber;
			ibValueQualifierDate* qDate = nullptr;
			if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
				m_typeDesc.m_typeData.m_date = *qDate;
			ibValueQualifierString* qString = nullptr;
			if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
				m_typeDesc.m_typeData.m_string = *qString;
			m_typeDesc.m_listTypeClass.emplace_back(
				activeMetaData->GetIDObjectFromString(classType)
			);
			return true;
		}
	}

	ibValueArray* valArray = nullptr;
	if (paParams[0]->ConvertToValue(valArray)) {
		ibValueQualifierNumber* qNumber = nullptr;
		if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
			m_typeDesc.m_typeData.m_number = *qNumber;
		ibValueQualifierDate* qDate = nullptr;
		if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
			m_typeDesc.m_typeData.m_date = *qDate;
		ibValueQualifierString* qString = nullptr;
		if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
			m_typeDesc.m_typeData.m_string = *qString;
		for (unsigned int i = 0; i < valArray->Count(); i++) {
			ibValue retValue; valArray->GetAt(i, retValue);
			ibValueType* valType = CastValue<ibValueType>(retValue);
			wxASSERT(valType);
			m_typeDesc.m_listTypeClass.emplace_back(
				valType->GetOwnerTypeClass()
			);
		}
		/*std::sort(m_listTypeClass.begin(), m_listTypeClass.end(), [](const ibClassID& a, const ibClassID& b) {
			return a < b; }
		);*/
		return true;
	}

	ibValueType* valType = nullptr;
	if (paParams[0]->ConvertToValue(valType)) {
		ibValueQualifierNumber* qNumber = nullptr;
		if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
			m_typeDesc.m_typeData.m_number = *qNumber;
		ibValueQualifierDate* qDate = nullptr;
		if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
			m_typeDesc.m_typeData.m_date = *qDate;
		ibValueQualifierString* qString = nullptr;
		if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
			m_typeDesc.m_typeData.m_string = *qString;
		m_typeDesc.m_listTypeClass.emplace_back(
			valType->GetOwnerTypeClass()
		);
		return true;
	}

	return false;
}

bool ibValueTypeDescription::ContainType(const ibValue& cType) const
{
	ibValueType* valueType = CastValue<ibValueType>(cType);
	wxASSERT(valueType);
	auto it = std::find(m_typeDesc.m_listTypeClass.begin(), m_typeDesc.m_listTypeClass.begin(), valueType->GetOwnerTypeClass());
	return it != m_typeDesc.m_listTypeClass.end();
}

ibValue ibValueTypeDescription::AdjustValue() const
{
	return AdjustValue(m_typeDesc);
}

ibValue ibValueTypeDescription::AdjustValue(const ibValue& varValue) const
{
	return AdjustValue(m_typeDesc, varValue);
}

ibValue ibValueTypeDescription::Types() const
{
	ibValueArray* arr = ibValue::CreateAndPrepareValueRef<ibValueArray>();
	for (auto clsid : m_typeDesc.m_listTypeClass)
		arr->Add(ibValue::CreateAndPrepareValueRef<ibValueType>(clsid));
	return arr;
}

enum Func {
	enContainsType = 0,
	enAdjustValue,
	enTypes
};

void ibValueTypeDescription::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendConstructor(4, wxT("typeDescription(type, qNumber, qDate, qString)"));

	m_methodHelper->AppendFunc(wxT("ContainType"), 1, wxT("containsType(type : type)"));
	m_methodHelper->AppendFunc(wxT("AdjustValue"), 1, wxT("AdjustValue(value = undefined : any"));
	m_methodHelper->AppendFunc(wxT("Types"), wxT("Types()"));
}

bool ibValueTypeDescription::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
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

VALUE_TYPE_REGISTER(ibValueType, "Type", string_to_clsid("VL_TYPE"));
VALUE_TYPE_REGISTER(ibValueTypeDescription, "TypeDescription", string_to_clsid("VL_TYPED"));

VALUE_TYPE_REGISTER(ibValueQualifierNumber, "QualifierNumber", string_to_clsid("VL_QNUM"));
VALUE_TYPE_REGISTER(ibValueQualifierDate, "QualifierDate", string_to_clsid("VL_QDAT"));
VALUE_TYPE_REGISTER(ibValueQualifierString, "QualifierString", string_to_clsid("VL_QSTR"));