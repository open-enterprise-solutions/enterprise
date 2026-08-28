#include "propertyDataComposition.h"

#include "backend/propertyManager/property/variant/variantComposition.h"

////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyDataComposition::CreateVariantData(ibPropertyObject* property, const ibCompositionDescription& desc) const
{
	return new ibVariantDataComposition(property, desc);
}

////////////////////////////////////////////////////////////////////////

ibCompositionDescription& ibPropertyDataComposition::GetValueAsCompositionDesc() const {
	return get_cell_variant<ibVariantDataComposition>()->GetCompositionDesc();
}

void ibPropertyDataComposition::SetValue(const ibCompositionDescription& val) {
	m_propValue = CreateVariantData(m_owner, val);
}

////////////////////////////////////////////////////////////////////////

// THE SAME TWO LINES a description-backed property always has. What a REPORT saves is a composer,
// and so is what a list saves — so the pair that reads and writes one is the pair that runs here.
bool ibPropertyDataComposition::ReadNodeValue(const ibDataValue& value)
{
	// …AND THE CONFIGURATION IT IS READ AGAINST — see ibPropertyComposition::ReadNodeValue for what
	// goes wrong without it, and why the owner is taken CONST here.
	const ibPropertyObject* owner = m_owner;
	return ibCompositionDescriptionMemory::ReadNode(value, GetValueAsCompositionDesc(),
		owner != nullptr ? owner->GetMetaData() : nullptr);
}

bool ibPropertyDataComposition::WriteNodeValue(ibDataValue& value) const
{
	return ibCompositionDescriptionMemory::WriteNode(value, GetValueAsCompositionDesc());
}
