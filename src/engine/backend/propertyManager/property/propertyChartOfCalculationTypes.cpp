#include "propertyChartOfCalculationTypes.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"

wxVariantData* ibPropertyChartOfCalculationTypes::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return new ibVariantDataOwner(property, typeDesc);
}

ibMetaDescription& ibPropertyChartOfCalculationTypes::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataMetaDesc>()->GetMetaDesc();
}

void ibPropertyChartOfCalculationTypes::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule — see propertyRecord.cpp.
void ibPropertyChartOfCalculationTypes::DoSetValue(const wxVariant& val)
{
	if (const ibVariantDataMetaDesc* carried = find_cell_variant<ibVariantDataMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

// The charts of calculation types in this configuration — one of them, or any set, as this property says.
ibPropertyChoiceMode ibPropertyChartOfCalculationTypes::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, m_choiceMode, { g_metaChartOfCalculationTypesCLSID });
}

bool ibPropertyChartOfCalculationTypes::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyChartOfCalculationTypes::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantDataOwner* owner = get_cell_variant<ibVariantDataOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyChartOfCalculationTypes::ReadNodeValue(const ibDataValue& value)
{
	ibMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
	return true;
}

bool ibPropertyChartOfCalculationTypes::WriteNodeValue(ibDataValue& value) const
{
	return ibMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc());
}
