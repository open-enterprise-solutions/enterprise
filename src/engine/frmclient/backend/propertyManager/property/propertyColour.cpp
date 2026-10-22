#include "propertyColour.h"
#include "core/serialize/dataBuilder.h"


wxString ibPropertyColour::GetValueAsString() const
{
	return GetNodeValue().AsString();
}

void ibPropertyColour::SetValue(const wxString& val)
{
	ibPropertySerializable::ReadNodeValue(ibDataValue::String(val));
}

bool ibPropertyColour::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyColour::SetValue(value.AsString());
	return true;
}