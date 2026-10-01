#include "propertyEnum.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — readable Number node value


//load & save object in control 
bool ibPropertyEnumBase::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyEnumBase::SetValue((long)value.AsInt()); // AsInt validates kind (Empty -> default)
	return true;
}

bool ibPropertyEnumBase::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::Int(ibPropertyEnumBase::GetValueAsInteger());
	return true;
}