#include "backend_type.h"

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

/////////////////////////////////////////////////////////////////////////////////////

ibClassID ibBackendTypeConfigFactory::GetDefaultTypeByFilter(ibSelectorDataType filterDataType)
{
	switch (filterDataType) {
	case ibSelectorDataType::ibSelectorDataType_boolean:  return g_valueBooleanCLSID;
	case ibSelectorDataType::ibSelectorDataType_resource: return g_valueNumberCLSID;
	case ibSelectorDataType::ibSelectorDataType_table:    return g_valueTableCLSID;
	case ibSelectorDataType::ibSelectorDataType_reference:
	default:                                              return g_valueStringCLSID;
	}
}
