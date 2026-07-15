#include "propertyDate.h"
#include "backend/serialize/dataBuilder.h"


//base property for "date"
bool ibPropertyDate::SetDataValue(const ibValue& varPropVal)
{
	ibPropertyDate::SetValue(varPropVal.GetDate());
	return true;
}

bool ibPropertyDate::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibPropertyDate::GetValueAsDateTime();
	return true;
}

bool ibPropertyDate::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyDate::SetValue((wxLongLong_t)value.AsDate());
	return true;
}

bool ibPropertyDate::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::Date(ibPropertyDate::GetValueAsDateTime());
	return true;
}