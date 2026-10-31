#include "propertySize.h"
#include "core/serialize/dataBuilder.h"


////////////////////////////////////////////////////////////////////////

ibString ibPropertySize::GetValueAsString() const
{
	return GetNodeValue().AsString();
}

void ibPropertySize::SetValue(const ibString& val)
{
	ibPropertySerializable::ReadNodeValue(ibDataValue::String(val));
}

////////////////////////////////////////////////////////////////////////

//base property for "size"
bool ibPropertySize::ReadNodeValue(const ibDataValue& value)
{
	ibPropertySize::SetValue(value.AsString());
	return true;
}