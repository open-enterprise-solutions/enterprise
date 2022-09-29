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

CValueTypeDescription::CValueTypeDescription() : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(NULL), m_qDate(NULL), m_qString(NULL)
{
	m_methods = new CMethods();
}

CValueTypeDescription::CValueTypeDescription(CValueType* valueType) : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(NULL), m_qDate(NULL), m_qString(NULL)
{
	m_methods = new CMethods();
	m_clsids.insert(valueType->GetOwnerType());
}

CValueTypeDescription::CValueTypeDescription(CValueType* valueType, CValueQualifierNumber* qNumber, CValueQualifierDate* qDate, CValueQualifierString* qString) : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(qNumber), m_qDate(qDate), m_qString(qString)
{
	m_methods = new CMethods();

	if (m_qNumber) {
		m_qNumber->IncrRef();
	}

	if (m_qDate) {
		m_qDate->IncrRef();
	}

	if (m_qString) {
		m_qString->IncrRef();
	}

	m_clsids.insert(valueType->GetOwnerType());
}

CValueTypeDescription::CValueTypeDescription(const std::set<CLASS_ID>& clsid) : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(NULL), m_qDate(NULL), m_qString(NULL), m_clsids(clsid)
{
	m_methods = new CMethods();
}

CValueTypeDescription::CValueTypeDescription(const std::set<CLASS_ID>& clsid, CValueQualifierNumber* qNumber, CValueQualifierDate* qDate, CValueQualifierString* qString) : CValue(eValueTypes::TYPE_VALUE, true),
m_qNumber(qNumber), m_qDate(qDate), m_qString(qString), m_clsids(clsid)
{
	m_methods = new CMethods();

	if (m_qNumber) {
		m_qNumber->IncrRef();
	}

	if (m_qDate) {
		m_qDate->IncrRef();
	}

	if (m_qString) {
		m_qString->IncrRef();
	}
}

CValueTypeDescription::~CValueTypeDescription()
{
	wxDELETE(m_methods);

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
	m_methods->AppendConstructor("typeDescription", "typeDescription(type, qNumber, qDate, qString)");

	SEng aMethods[] = {
		{"containsType","containsType(type)"},
		{"adjustValue","adjustValue(value = undefined)"},
		{"types","types()"},
	};
	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods->PrepareMethods(aMethods, nCountM);
}

CValue CValueTypeDescription::Method(methodArg_t& aParams)
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

bool CValueTypeDescription::Init(CValue** aParams)
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

			m_clsids.insert(
				metadata->GetIDObjectFromString(classType)
			);
			return true;
		}
	}

	CValueArray* valArray = NULL;

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
			CValueType* valType = value_cast<CValueType>(valArray->GetAt(i));
			wxASSERT(valType);
			m_clsids.insert(
				valType->GetOwnerType()
			);
		}

		/*std::sort(m_clsids.begin(), m_clsids.end(), [](const CLASS_ID& a, const CLASS_ID& b) {
			return a < b; }
		);*/

		return true;
	}

	CValueType* valType = NULL;
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

		m_clsids.insert(
			valType->GetOwnerType()
		);
		return true;
	}

	return false;
}

bool CValueTypeDescription::ContainsType(const CValue& cType) const
{
	CValueType* valueType = value_cast<CValueType>(cType);
	wxASSERT(valueType);
	auto itFounded = std::find(m_clsids.begin(), m_clsids.begin(), valueType->GetOwnerType());
	return itFounded != m_clsids.end();
}

CValue CValueTypeDescription::AdjustValue() const
{
	if (1 == m_clsids.size()) {
		auto posIt = m_clsids.begin();
		std::advance(posIt, 0);
		wxString metaName = metadata->GetNameObjectFromID(*posIt);
		return metadata->CreateObject(metaName);
	}

	return CValue();
}

#include "compiler/systemObjects.h"

CValue CValueTypeDescription::AdjustValue(const CValue& cVal) const
{
	auto foundedIt = std::find(m_clsids.begin(), m_clsids.end(), cVal.GetClassType());
	if (foundedIt != m_clsids.end()) {
		eValueTypes vt = CValue::GetVTByID(cVal.GetClassType());
		if (vt < eValueTypes::TYPE_REFFER) {
			if (vt == eValueTypes::TYPE_NUMBER &&
				m_qNumber) {
				return CSystemObjects::Round(cVal, m_qNumber->m_scale);
			}
			else if (vt == eValueTypes::TYPE_DATE &&
				m_qDate) {
				if (m_qDate->m_dateTime == eDateFractions::eDateFractions_Date) {
					return cVal;
				}
				else if (m_qDate->m_dateTime == eDateFractions::eDateFractions_Time) {
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

	if (m_clsids.size() == 1) {
		auto posIt = m_clsids.begin();
		std::advance(posIt, 0);
		if (metadata->IsRegisterObject(*posIt)) {
			eValueTypes vt = CValue::GetVTByID(*posIt);
			if (vt < eValueTypes::TYPE_REFFER) {
				if (vt == eValueTypes::TYPE_NUMBER) {
					return m_qNumber ? CSystemObjects::Round(cVal, m_qNumber->m_scale) : cVal.GetNumber();
				}
				else if (vt == eValueTypes::TYPE_DATE) {
					if (m_qDate && m_qDate->m_dateTime == eDateFractions::eDateFractions_Date) {
						return cVal;
					}
					else if (m_qDate && m_qDate->m_dateTime == eDateFractions::eDateFractions_Time) {
						return cVal;
					}
					return cVal.GetDate();
				}
				else if (vt == eValueTypes::TYPE_STRING) {
					return m_qString ? 
						CSystemObjects::Left(cVal, m_qString->m_length) :
						cVal.GetString();
				}
			}
			wxString metaName = metadata->GetNameObjectFromID(*posIt);
			return metadata->CreateObject(metaName);
		}
	}

	return CValue();
}

CValue CValueTypeDescription::Types() const
{
	std::vector<CValue> allTypes;
	for (auto type_id : m_clsids) {
		allTypes.push_back(new CValueType(type_id));
	}
	return new CValueArray(allTypes);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueTypeDescription, "typeDescription", TEXT2CLSID("VL_TYDE"));