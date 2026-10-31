#include "propertyFont.h"
#include "core/serialize/dataBuilder.h"


ibString ibPropertyFont::GetValueAsString() const
{
	return GetNodeValue().AsString();
}

void ibPropertyFont::SetValue(const ibString& val)
{
	ibPropertySerializable::ReadNodeValue(ibDataValue::String(val));
}

bool ibPropertyFont::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyFont::SetValue(value.AsString());
	return true;
}