#include "value.h"

bool ibValue::DoSerialize(wxString& strValue) const
{
	if (IsReference() && m_pRef != nullptr)
		return m_pRef->DoSerialize(strValue);

	switch (m_typeClass) {
	case ibValueTypes::TYPE_NULL:
		strValue = wxEmptyString;
		return true;
	case ibValueTypes::TYPE_BOOLEAN:
		strValue = m_bData ? wxT("true") : wxT("false");
		return true;
	case ibValueTypes::TYPE_NUMBER:
		strValue = m_fData.ToString();
		return true;
	case ibValueTypes::TYPE_STRING:
		strValue = GetString();
		return true;
	case ibValueTypes::TYPE_DATE:
		strValue = wxString::Format(wxT("%lld"), m_dData);
		return true;
	default:
		break;
	}

	return false;
}

bool ibValue::DoDeserialize(const wxString& strValue)
{
	if (m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->DoDeserialize(strValue);

	switch (m_typeClass) {
	case ibValueTypes::TYPE_NULL:
		return true;
	case ibValueTypes::TYPE_BOOLEAN:
		m_bData = (strValue == wxT("true") || strValue == wxT("1"));
		return true;
	case ibValueTypes::TYPE_NUMBER: {
		double dVal = 0;
		if (strValue.ToDouble(&dVal)) {
			m_fData = ibNumber(dVal);
			return true;
		}
		return false;
	}
	case ibValueTypes::TYPE_STRING:
		SetString(strValue);
		return true;
	case ibValueTypes::TYPE_DATE: {
		wxLongLong_t val = 0;
		if (strValue.ToLongLong(&val)) {
			m_dData = val;
			return true;
		}
		return false;
	}
	default:
		break;
	}

	return false;
}
