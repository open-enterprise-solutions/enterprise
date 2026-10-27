#include "propertyChartOfCharacteristicTypes.h"
#include "core/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"


ibVariant ibPropertyChartOfCharacteristicTypes::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc)
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return ibVariant(new ibVariantOwner(property, typeDesc));
}

ibMetaDescription& ibPropertyChartOfCharacteristicTypes::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantMetaDesc>()->GetMetaDesc();
}

void ibPropertyChartOfCharacteristicTypes::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule - see propertyRecord.cpp.
void ibPropertyChartOfCharacteristicTypes::DoSetValue(const ibVariant& val)
{
	if (const ibVariantMetaDesc* carried = find_cell_variant<ibVariantMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

// The charts of characteristic types in this configuration. The list used to sit in
// advpropChartOfCharacteristicTypes.cpp — where a chart of accounts could not reach it.
ibPropertyChoiceMode ibPropertyChartOfCharacteristicTypes::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, ibPropertyChoiceMode::Single, { g_metaChartOfCharacteristicTypesCLSID });
}

bool ibPropertyChartOfCharacteristicTypes::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyChartOfCharacteristicTypes::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantOwner* owner = get_cell_variant<ibVariantOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyChartOfCharacteristicTypes::ReadNodeValue(const ibDataValue& value)
{
	return ibBackendMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
}

bool ibPropertyChartOfCharacteristicTypes::WriteNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	return ibBackendMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc(), owner->GetMetaData());
}
