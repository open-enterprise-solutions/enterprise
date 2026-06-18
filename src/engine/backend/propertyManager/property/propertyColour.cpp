#include "propertyColour.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueColour.h"

// get property for grid	
wxObject* (*ibPropertyColour::ms_propertyColour)(const wxString&, const wxString&, const wxColour&) = nullptr;

//base property for "colour"
bool ibPropertyColour::SetDataValue(const ibValue& varPropVal)
{
	ibValueColour* valueColour = varPropVal.ConvertToType<ibValueColour>();
	if (valueColour == nullptr)
		return false;
	SetValue(valueColour->m_colour);
	return true;
}

bool ibPropertyColour::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue<ibValueColour>(GetValueAsColour());
	return true;
}

bool ibPropertyColour::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyColour::SetValue(value.AsString());
	return true;
}

bool ibPropertyColour::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::String(ibPropertyColour::GetValueAsString());
	return true;
}