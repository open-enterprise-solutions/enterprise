#include "propertyChartOfCharacteristicTypes.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"


wxVariantData* ibPropertyChartOfCharacteristicTypes::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return new ibVariantDataOwner(property, typeDesc);
}

ibMetaDescription& ibPropertyChartOfCharacteristicTypes::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataOwner>()->GetMetaDesc();
}

void ibPropertyChartOfCharacteristicTypes::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
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
	const ibVariantDataOwner* owner = get_cell_variant<ibVariantDataOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyChartOfCharacteristicTypes::ReadNodeValue(const ibDataValue& value)
{
	return ibMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
}

bool ibPropertyChartOfCharacteristicTypes::WriteNodeValue(ibDataValue& value) const
{
	return ibMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc());
}
