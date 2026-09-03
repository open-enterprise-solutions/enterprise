#include "propertyString.h"
#include "backend/propertyManager/property/variant/variantTranslate.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (String, the stored form)


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyTString::CreateVariantData(const ibTranslateString& translate) const
{
	return new ibVariantDataTranslate(translate);
}

////////////////////////////////////////////////////////////////////////

ibTranslateString& ibPropertyTString::GetValueAsTranslate() const
{
	return get_cell_variant<ibVariantDataTranslate>()->GetTranslate();
}

void ibPropertyTString::SetValue(const ibTranslateString& translate)
{
	m_propValue = CreateVariantData(translate);
}

// ⭐ A SCRIPT WRITES ONE TEXT, and that is the language in force — so the cells are EDITED, not
// replaced. A plain assignment is how every other language used to be lost.
//base property for "caption" - for translate
bool ibPropertyTString::SetDataValue(const ibValue& varPropVal)
{
	ibPropertyTString::GetValueAsTranslate().SetTranslate(varPropVal.GetString());
	return true;
}

bool ibPropertyTString::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibPropertyTString::GetValueAsTranslate().GetString();
	return true;
}

// ⚠ STORED AS THE RAW FORM, unchanged. A configuration written before this reads as it always did,
// and one written now is still read by everything that speaks the format — the value moved into a
// type of its own, the file did not move with it.
bool ibPropertyTString::ReadNodeValue(const ibDataValue& value)
{
	GetValueAsTranslate().SetRawText(value.AsString());
	return true;
}

bool ibPropertyTString::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::String(GetValueAsTranslate().GetRawText());
	return true;
}
