#include "propertyBoolean.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — readable Bool node value


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
	// An EMPTY value never arrives here — ibBackendProperty::SetNodeValue gates it, so a boolean
	// missing from the file keeps the default it was declared with instead of being assigned
	// false. (That conflation is what unset `Correspondence` / `SplitTotals` on load; the reason
	// is written once, at the gate.)
	ibPropertyBoolean::SetValue(value.AsBool());
	return true;
}

bool ibPropertyBoolean::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::Bool(ibPropertyBoolean::GetValueAsBoolean());
	return true;
}