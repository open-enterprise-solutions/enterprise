#include "propertyOwner.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"


////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyOwner::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return new ibVariantDataOwner(property, typeDesc);
}

ibMetaDescription& ibPropertyOwner::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataMetaDesc>()->GetMetaDesc();
}

void ibPropertyOwner::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule - see propertyRecord.cpp. It costs nothing here today, because CreateValueList
// happens to build ibVariantDataOwner and this is the property that holds one; keeping the rule in
// all five is what stops that coincidence from being load-bearing.
void ibPropertyOwner::DoSetValue(const wxVariant& val)
{
	if (const ibVariantDataMetaDesc* carried = find_cell_variant<ibVariantDataMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

// A catalog is owned by a catalog. The list used to sit in advpropOwner.cpp's constructor.
ibPropertyChoiceMode ibPropertyOwner::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, ibPropertyChoiceMode::Mult, { g_metaCatalogCLSID });
}

//base property for "owner"
bool ibPropertyOwner::SetDataValue(const ibValue& varPropVal)
{
	return false;
}

bool ibPropertyOwner::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantDataOwner* owner = get_cell_variant<ibVariantDataOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyOwner::ReadNodeValue(const ibDataValue& value)
{
	return ibMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
}

bool ibPropertyOwner::WriteNodeValue(ibDataValue& value) const
{
	return ibMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc());
}
