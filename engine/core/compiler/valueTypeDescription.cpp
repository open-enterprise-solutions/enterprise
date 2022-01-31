////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value type description (collection types)
////////////////////////////////////////////////////////////////////////////

#include "valueTypeDescription.h"
#include "valueType.h"
#include "valueArray.h"
#include "methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueTypeDescription, CValue);

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CMethods CValueTypeDescription::m_methods;

CValueTypeDescription::CValueTypeDescription() : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(NULL), m_qDate(NULL), m_qString(NULL)
{
}

CValueTypeDescription::CValueTypeDescription(CValueType *valueType) : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(NULL), m_qDate(NULL), m_qString(NULL)
{
	m_aTypes.push_back(valueType->GetOwnerType());
}

CValueTypeDescription::CValueTypeDescription(CValueType *valueType, CValueQualifierNumber *qNumber, CValueQualifierDate *qDate, CValueQualifierString *qString) : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(qNumber), m_qDate(qDate), m_qString(qString)
{
	if (m_qNumber) {
		m_qNumber->IncrRef();
	}

	if (m_qDate) {
		m_qDate->IncrRef();
	}

	if (m_qString) {
		m_qString->IncrRef();
	}

	m_aTypes.push_back(valueType->GetOwnerType());
}

CValueTypeDescription::CValueTypeDescription(const CLASS_ID &clsid) : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(NULL), m_qDate(NULL), m_qString(NULL)
{
	m_aTypes.push_back(clsid);
}

CValueTypeDescription::CValueTypeDescription(const CLASS_ID &clsid, CValueQualifierNumber *qNumber, CValueQualifierDate *qDate, CValueQualifierString *qString) : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(qNumber), m_qDate(qDate), m_qString(qString)
{
	if (m_qNumber) {
		m_qNumber->IncrRef();
	}

	if (m_qDate) {
		m_qDate->IncrRef();
	}

	if (m_qString) {
		m_qString->IncrRef();
	}

	m_aTypes.push_back(clsid);
}

CValueTypeDescription::~CValueTypeDescription()
{
	if (m_qNumber) {
		m_qNumber->DecrRef();
	}

	if (m_qDate) {
		m_qDate->DecrRef();
	}

	if (m_qString) {
		m_qString->DecrRef();
	}
}

enum
{
	enContainsType = 0,
	enAdjustValue,
	enTypes
};

void CValueTypeDescription::PrepareNames() const
{
	m_methods.AppendConstructor("typeDescription", "typeDescription(type, qNumber, qDate, qString)");

	SEng aMethods[] = {
		{"containsType","containsType(type)"},
		{"adjustValue","adjustValue(value = undefined)"},
		{"types","types()"},
	};
	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods.PrepareMethods(aMethods, nCountM);
}

CValue CValueTypeDescription::Method(methodArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enContainsType: return ContainsType(aParams[0]);
	case enAdjustValue: {
		if (aParams.GetParamCount() > 0) {
			return AdjustValue(aParams[0]);
		}
		return AdjustValue();
	}
	case enTypes: return Types();
	}

	return CValue();
}

#include "metadata/metadata.h"

bool CValueTypeDescription::Init(CValue **aParams)
{
	if (aParams[0]->GetType() == eValueTypes::TYPE_STRING) {
		wxString classType = aParams[0]->GetString();
		if (metadata->IsRegisterObject(classType)) {

			int argCount = sizeof(aParams) / sizeof(aParams[0]); 

			if (argCount > 1 && aParams[1]->ConvertToValue(m_qNumber)) {
				m_qNumber->IncrRef();
			}

			if (argCount > 2 && aParams[2]->ConvertToValue(m_qDate)) {
				m_qDate->IncrRef();
			}

			if (argCount > 3 && aParams[3]->ConvertToValue(m_qString)) {
				m_qString->IncrRef();
			}

			m_aTypes.push_back(metadata->GetIDObjectFromString(classType));
			return true;
		}
	}

	CValueArray *valArray = NULL;

	if (aParams[0]->ConvertToValue(valArray)) {

		int argCount = sizeof(aParams) / sizeof(aParams[0]);

		if (argCount > 1 && aParams[1]->ConvertToValue(m_qNumber)) {
			m_qNumber->IncrRef();
		}

		if (argCount > 2 && aParams[2]->ConvertToValue(m_qDate)) {
			m_qDate->IncrRef();
		}

		if (argCount > 3 && aParams[3]->ConvertToValue(m_qString)) {
			m_qString->IncrRef();
		}

		for (unsigned int i = 0; i < valArray->Count(); i++) {
			CValueType *valType = value_cast<CValueType>(valArray->GetAt(i));
			wxASSERT(valType);
			m_aTypes.push_back(valType->GetOwnerType());
		}
		std::sort(m_aTypes.begin(), m_aTypes.end());
		return true;
	}

	CValueType *valType = NULL;
	if (aParams[0]->ConvertToValue(valType)) {

		int argCount = sizeof(aParams) / sizeof(aParams[0]);

		if (argCount > 1 && aParams[1]->ConvertToValue(m_qNumber)) {
			m_qNumber->IncrRef();
		}

		if (argCount > 2 && aParams[2]->ConvertToValue(m_qDate)) {
			m_qDate->IncrRef();
		}

		if (argCount > 3 && aParams[3]->ConvertToValue(m_qString)) {
			m_qString->IncrRef();
		}

		m_aTypes.push_back(valType->GetOwnerType());
		return true;
	}

	return false;
}

bool CValueTypeDescription::ContainsType(const CValue &cType) const
{
	CValueType *valueType = value_cast<CValueType>(cType);
	wxASSERT(valueType);
	auto itFounded = std::find(m_aTypes.begin(), m_aTypes.begin(), valueType->GetOwnerType());
	return itFounded != m_aTypes.end();
}

CValue CValueTypeDescription::AdjustValue() const
{
	if (1 == m_aTypes.size()) {
		wxString metaName = metadata->GetNameObjectFromID(m_aTypes[0]);
		return metadata->CreateObject(metaName);
	}

	return CValue();
}

#include "compiler/systemObjects.h"

CValue CValueTypeDescription::AdjustValue(const CValue &cVal) const
{
	auto foundedIt = std::find(m_aTypes.begin(), m_aTypes.end(), cVal.GetTypeID());
	if (foundedIt != m_aTypes.end()) {
		meta_identifier_t vt = metadata->GetVTByID(cVal.GetTypeID());
		if (vt < defaultMetaID) {
			if (vt == eValueTypes::TYPE_NUMBER &&
				m_qNumber) {
				return CSystemObjects::Round(cVal, m_qNumber->m_scale);
			}
			else if (vt == eValueTypes::TYPE_DATE &&
				m_qDate) {
				if (m_qDate->m_dateTime == eDateFractions::eDate) {
					return cVal;
				}
				else if (m_qDate->m_dateTime == eDateFractions::eTime) {
					return cVal;
				}
				return cVal;
			}
			else if (vt == eValueTypes::TYPE_STRING &&
				m_qString) {
				return CSystemObjects::Left(cVal, m_qString->m_length);
			}
		}
		return cVal;
	}

	for (auto type_id : m_aTypes) {
		meta_identifier_t vt = metadata->GetVTByID(type_id);
		if (vt < defaultMetaID) {
			
			if (vt == eValueTypes::TYPE_NUMBER &&
				m_qNumber) {
				return CSystemObjects::Round(cVal, m_qNumber->m_scale);
			}
			else if (vt == eValueTypes::TYPE_DATE &&
				m_qDate) {
				if (m_qDate->m_dateTime == eDateFractions::eDate) {
					return cVal;
				}
				else if (m_qDate->m_dateTime == eDateFractions::eTime) {
					return cVal;
				}
				return cVal;
			}
			else if (vt == eValueTypes::TYPE_STRING &&
				m_qString) {
				return CSystemObjects::Left(cVal, m_qString->m_length);
			}

			else if (vt == eValueTypes::TYPE_BOOLEAN) {
				return cVal.GetBoolean();
			}
			else if (vt == eValueTypes::TYPE_NUMBER) {
				return cVal.GetNumber();
			}
			else if (vt == eValueTypes::TYPE_STRING) {
				return cVal.GetString();
			}
			else if (vt == eValueTypes::TYPE_DATE) {
				return cVal.GetDate();
			}
			
			return CValue(cVal.m_typeClass);
		}
	}

	if (m_aTypes.size() == 1) {
		wxString metaName = metadata->GetNameObjectFromID(m_aTypes[0]);
		return metadata->CreateObject(metaName);
	}

	return CValue();
}

CValue CValueTypeDescription::Types() const
{
	std::vector<CValue> allTypes;
	for (auto type_id : m_aTypes) {
		allTypes.push_back(new CValueType(type_id));
	}
	return new CValueArray(allTypes);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueTypeDescription, "typeDescription", TEXT2CLSID("VL_TYDE"));