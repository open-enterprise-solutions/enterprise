#include "propertyChartOfAccounts.h"
#include "core/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"


ibVariant ibPropertyChartOfAccounts::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc)
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return ibVariant(new ibVariantOwner(property, typeDesc));
}

ibMetaDescription& ibPropertyChartOfAccounts::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantMetaDesc>()->GetMetaDesc();
}

void ibPropertyChartOfAccounts::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule - see propertyRecord.cpp.
void ibPropertyChartOfAccounts::DoSetValue(const ibVariant& val)
{
	if (const ibVariantMetaDesc* carried = find_cell_variant<ibVariantMetaDesc>(val)) {
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
	const ibVariantOwner* owner = get_cell_variant<ibVariantOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyChartOfAccounts::ReadNodeValue(const ibDataValue& value)
{
	ibBackendMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
	return true;
}

bool ibPropertyChartOfAccounts::WriteNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	return ibBackendMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc(), owner->GetMetaData());
}
