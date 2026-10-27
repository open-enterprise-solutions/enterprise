#include "propertyString.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — readable String node value


//base property for "string" — held as its node (ibPropertySerializable::ReadNodeValue)
ibString ibPropertyStringBase::GetValueAsString() const
{
	return GetNodeValue().AsString();
}

void ibPropertyStringBase::SetValue(const ibString& strValue)
{
	ibPropertySerializable::ReadNodeValue(ibDataValue::String(strValue));
}

bool ibPropertyStringBase::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyStringBase::SetValue(value.AsString()); // AsString validates kind (Empty -> default "")
	return true;
}