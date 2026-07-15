#include "propertyFont.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueFont.h"


//base property for "colour"
bool ibPropertyFont::SetDataValue(const ibValue& varPropVal)
{
	ibValueFont* valueFont = varPropVal.ConvertToType<ibValueFont>();
	if (valueFont == nullptr)
		return false;
	SetValue(valueFont->m_font);
	return true;
}

bool ibPropertyFont::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue<ibValueFont>(GetValueAsFont());
	return true;
}

bool ibPropertyFont::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyFont::SetValue(value.AsString());
	return true;
}

bool ibPropertyFont::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::String(ibPropertyFont::GetValueAsString());
	return true;
}