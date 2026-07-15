#include "propertyEnum.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — readable Number node value

//get property for grid
wxObject* (*ibPropertyEnumBase::ms_propertyEnum)(const wxString&, const wxString&, const ibPropertyChoiceList&, const int&) = nullptr;

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