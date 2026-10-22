#include "propertySize.h"
#include "core/serialize/dataBuilder.h"


////////////////////////////////////////////////////////////////////////

wxString ibPropertySize::GetValueAsString() const
{
	return GetNodeValue().AsString();
}

void ibPropertySize::SetValue(const wxString& val)
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