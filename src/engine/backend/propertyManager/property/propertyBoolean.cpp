#include "propertyBoolean.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — readable Bool node value

//get property for grid
wxObject* (*ibPropertyBoolean::ms_propertyBoolean)(const wxString&, const wxString&, const bool&) = nullptr;

//base property for "bool"
bool ibPropertyBoolean::SetDataValue(const ibValue& varPropVal)
{
	SetValue(varPropVal.GetBoolean());
	return true;
}

bool ibPropertyBoolean::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibPropertyBoolean::GetValueAsBoolean();
	return true;
}

bool ibPropertyBoolean::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyBoolean::SetValue(value.AsBool()); // AsBool validates kind (Empty -> default false)
	return true;
}

bool ibPropertyBoolean::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::Bool(ibPropertyBoolean::GetValueAsBoolean());
	return true;
}