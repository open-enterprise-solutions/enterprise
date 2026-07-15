#include "propertySize.h"
#include "backend/propertyManager/property/variant/variantSize.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueSize.h"


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertySize::CreateVariantData(const wxSize& val)
{
	return new ibVariantDataSize(val);
}

wxSize ibPropertySize::GetValueAsSize() const
{
	return get_cell_variant<ibVariantDataSize>()->GetSize();
}

////////////////////////////////////////////////////////////////////////

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