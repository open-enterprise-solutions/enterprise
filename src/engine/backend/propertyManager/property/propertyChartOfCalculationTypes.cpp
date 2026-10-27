#include "propertyChartOfCalculationTypes.h"
#include "core/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"

ibPropertyValue ibPropertyChartOfCalculationTypes::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc)
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return ibPropertyValue(new ibPropertyValueOwner(property, typeDesc));
}

ibMetaDescription& ibPropertyChartOfCalculationTypes::GetValueAsMetaDesc() const {
	return get_cell_variant<ibPropertyValueMetaDesc>()->GetMetaDesc();
}

void ibPropertyChartOfCalculationTypes::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule — see propertyRecord.cpp.
void ibPropertyChartOfCalculationTypes::DoSetValue(const ibPropertyValue& val)
{
	if (const ibPropertyValueMetaDesc* carried = find_cell_variant<ibPropertyValueMetaDesc>(val)) {
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
	const ibPropertyValueOwner* owner = get_cell_variant<ibPropertyValueOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyChartOfCalculationTypes::ReadNodeValue(const ibDataValue& value)
{
	ibBackendMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
	return true;
}

bool ibPropertyChartOfCalculationTypes::WriteNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	return ibBackendMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc(), owner->GetMetaData());
}
