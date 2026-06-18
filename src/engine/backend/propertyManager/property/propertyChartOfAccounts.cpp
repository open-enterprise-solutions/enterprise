#include "propertyChartOfAccounts.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"

wxObject* (*ibPropertyChartOfAccounts::ms_propertyChartOfAccounts)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&) = nullptr;

wxVariantData* ibPropertyChartOfAccounts::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	const ibValueMetaObjectGenericData* propFactory = dynamic_cast<const ibValueMetaObjectGenericData*>(property);
	if (propFactory == nullptr) return nullptr;
	return new ibVariantDataOwner(propFactory, typeDesc);
}

ibMetaDescription& ibPropertyChartOfAccounts::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataOwner>()->GetMetaDesc();
}

void ibPropertyChartOfAccounts::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
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
