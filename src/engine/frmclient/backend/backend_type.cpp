#include "backend_type.h"

#include "frmclient/backend/formatString.h"
#include "frmclient/backend/system/value/valueType.h"   // ibValueTypeDescription::AdjustValue — the one door a type makes a value through

ibValue ibBackendTypeFactory::CreateValue() const
{
	// The type's empty value — the engine made an enumeration's first member here, and an enumeration is the server's.
	return ibValueTypeDescription::AdjustValue(GetTypeValueDesc());
}

ibValue ibBackendTypeFactory::AdjustValue() const
{
	return ibValueTypeDescription::AdjustValue(GetTypeValueDesc());
}

ibValue ibBackendTypeFactory::AdjustValue(const ibValue& varValue) const
{
	return ibValueTypeDescription::AdjustValue(GetTypeValueDesc(), varValue);
}

ibValue ibBackendTypeFactory::AdjustValue(const ibValue& varValue, const ibTypeDescription& limit) const
{
	return ibValueTypeDescription::AdjustValue(limit, varValue);
}

bool ibBackendTypeConfigFactory::GetFormatFromTypeDesc(const ibTypeDescription& type, ibFormatString& formatString)
{
	bool written = false;

	// A number: as many digits after the point as the type keeps — `5.00`, not `5`. A number nobody bounded
	// (precision 0, "no limit" — an average, a product) keeps no count of its own and is shown as it is.
	if (type.ContainType(ibValueTypes::TYPE_NUMBER) && type.GetPrecision() > 0) {
		formatString.m_number.m_fractionDigits = type.GetScale();
		written = true;
	}

	// A date: what its fractions keep — a date alone shows no time, a time no date.
	if (type.ContainType(ibValueTypes::TYPE_DATE)) {
		switch (type.GetDateFraction()) {
		case ibDateFractions::ibDateFractions_Date:
			formatString.m_date.m_pattern = ibFormatString::PresetPattern(ibDatePreset::Date);
			break;
		case ibDateFractions::ibDateFractions_Time:
			formatString.m_date.m_pattern = ibFormatString::PresetPattern(ibDatePreset::Time);
			break;
		default:
			formatString.m_date.m_pattern = ibFormatString::PresetPattern(ibDatePreset::DateTime);
			break;
		}
		written = true;
	}

	return written;
}
