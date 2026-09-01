#include "propertyNumber.h"
#include "backend/propertyManager/property/variant/variantNumber.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyNumber::CreateVariantData(const ibNumber& val)
{
	return new ibVariantDataNumber(val);
}

////////////////////////////////////////////////////////////////////////

ibNumber& ibPropertyNumber::GetValueAsNumber() const
{
	return get_cell_variant<ibVariantDataNumber>()->GetNumber();
}

void ibPropertyNumber::SetValue(const ibNumber& val)
{
	m_propValue = CreateVariantData(val);
}

//base property for "number"
bool ibPropertyNumber::SetDataValue(const ibValue& varPropVal)
{
	SetValue(varPropVal.GetNumber());
	return true;
}

bool ibPropertyNumber::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibPropertyNumber::GetValueAsNumber();
	return true;
}

// ⭐ A NUMBER GOES IN AS A NUMBER. It used to travel as the buffer ibNumber packs
// itself into — which is exact and unreadable, and unnecessary: the node HAS a
// Number payload and it is ibNumber itself, so nothing is rounded, nothing is
// narrowed, and a value with two hundred fractional digits survives intact
// (Max, 2026-08-30: *"we do not care how it is stored — as a number, and however
// huge it is we will still read it"*).
//
// What this buys is that the JSON view of a property shows the number instead of
// base64. That is the whole of the migration onto the node, said once more.

bool ibPropertyNumber::ReadNodeValue(const ibDataValue& value)
{
	GetValueAsNumber() = value.AsNumber();
	return true;
}

bool ibPropertyNumber::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::Number(GetValueAsNumber());
	return true;
}