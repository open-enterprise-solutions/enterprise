#include "propertyChartOfAccounts.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"


wxVariantData* ibPropertyChartOfAccounts::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return new ibVariantDataOwner(property, typeDesc);
}

ibMetaDescription& ibPropertyChartOfAccounts::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataMetaDesc>()->GetMetaDesc();
}

void ibPropertyChartOfAccounts::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule - see propertyRecord.cpp.
void ibPropertyChartOfAccounts::DoSetValue(const wxVariant& val)
{
	if (const ibVariantDataMetaDesc* carried = find_cell_variant<ibVariantDataMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

// The charts of accounts in this configuration. The list used to sit in advpropChartOfAccounts.cpp.
ibPropertyChoiceMode ibPropertyChartOfAccounts::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, ibPropertyChoiceMode::Single, { g_metaChartOfAccountsCLSID });
}

bool ibPropertyChartOfAccounts::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyChartOfAccounts::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantDataOwner* owner = get_cell_variant<ibVariantDataOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyChartOfAccounts::ReadNodeValue(const ibDataValue& value)
{
	ibMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
	return true;
}

bool ibPropertyChartOfAccounts::WriteNodeValue(ibDataValue& value) const
{
	return ibMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc());
}
