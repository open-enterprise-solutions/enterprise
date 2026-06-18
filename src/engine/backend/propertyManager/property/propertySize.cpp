#include "propertySize.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueSize.h"

wxObject* (*ibPropertySize::ms_propertySize)(const wxString&, const wxString&, const wxSize&) = nullptr;

//base property for "size"
bool ibPropertySize::SetDataValue(const ibValue& varPropVal)
{
	ibValueSize* valueSize = varPropVal.ConvertToType<ibValueSize>();
	if (valueSize == nullptr)
		return false;
	SetValue(valueSize->m_size);
	return true;
}

bool ibPropertySize::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue <ibValueSize>(GetValueAsSize());
	return true;
}

bool ibPropertySize::ReadNodeValue(const ibDataValue& value)
{
	ibPropertySize::SetValue(value.AsString());
	return true;
}

bool ibPropertySize::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::String(ibPropertySize::GetValueAsString());
	return true;
}