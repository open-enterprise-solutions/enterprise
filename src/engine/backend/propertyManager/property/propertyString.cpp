#include "propertyString.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — readable String node value


//base property for "string"
bool ibPropertyStringBase::SetDataValue(const ibValue& varPropVal)
{
	ibPropertyStringBase::SetValue(varPropVal.GetString());
	return true;
}

bool ibPropertyStringBase::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibPropertyStringBase::GetValueAsString();
	return true;
}

bool ibPropertyStringBase::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyStringBase::SetValue(value.AsString()); // AsString validates kind (Empty -> default "")
	return true;
}

bool ibPropertyStringBase::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::String(ibPropertyStringBase::GetValueAsString());
	return true;
}