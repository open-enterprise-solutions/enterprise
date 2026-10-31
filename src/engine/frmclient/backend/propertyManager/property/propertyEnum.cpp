#include "propertyEnum.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — readable Number node value


long ibPropertyEnumBase::GetValueAsInteger() const
{
	return static_cast<long>(GetNodeValue().AsInt());
}

void ibPropertyEnumBase::SetValue(const long& i)
{
	ibPropertySerializable::ReadNodeValue(ibDataValue::Int(i));
}

//load & save object in control
bool ibPropertyEnumBase::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyEnumBase::SetValue((long)value.AsInt()); // AsInt validates kind (Empty -> default)
	return true;
}