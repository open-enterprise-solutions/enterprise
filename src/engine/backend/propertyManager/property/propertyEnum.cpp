#include "propertyEnum.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — readable Number node value


//load & save object in control 
bool ibPropertyEnumBase::ReadNodeValue(const ibDataValue& value)
{
	const long read = (long)value.AsInt(); // AsInt validates kind (Empty -> default)
	if (!HasEnumValue(read))
		return false;
	ibPropertyEnumBase::SetValue(read);
	return true;
}

bool ibPropertyEnumBase::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::Int(ibPropertyEnumBase::GetValueAsInteger());
	return true;
}