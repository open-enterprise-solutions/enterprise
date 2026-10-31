#include "propertyColour.h"
#include "core/serialize/dataBuilder.h"


ibString ibPropertyColour::GetValueAsString() const
{
	return GetNodeValue().AsString();
}

void ibPropertyColour::SetValue(const ibString& val)
{
	ibPropertySerializable::ReadNodeValue(ibDataValue::String(val));
}

bool ibPropertyColour::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyColour::SetValue(value.AsString());
	return true;
}