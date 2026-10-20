#include "propertyString.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — readable String node value


//base property for "string" — held as its node (ibBackendProperty::ReadNodeValue)
wxString ibPropertyStringBase::GetValueAsString() const
{
	return GetNodeValue().AsString();
}

void ibPropertyStringBase::SetValue(const wxString& strValue)
{
	ibProperty::ReadNodeValue(ibDataValue::String(strValue));
}

bool ibPropertyStringBase::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyStringBase::SetValue(value.AsString()); // AsString validates kind (Empty -> default "")
	return true;
}