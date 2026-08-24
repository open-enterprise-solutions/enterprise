#include "propertyDynamicList.h"


#include "backend/propertyManager/property/variant/variantComposition.h"

////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyDynamicList::CreateVariantData(ibPropertyObject* property, const ibCompositionDescription& desc) const
{
	return new ibVariantDataComposition(property, desc);
}

////////////////////////////////////////////////////////////////////////

ibCompositionDescription& ibPropertyDynamicList::GetValueAsCompositionDesc() const {
	return get_cell_variant<ibVariantDataComposition>()->GetCompositionDesc();
}

void ibPropertyDynamicList::SetValue(const ibCompositionDescription& val) {
	m_propValue = CreateVariantData(m_owner, val);
}

////////////////////////////////////////////////////////////////////////

// THE SAME TWO LINES a description-backed property always has. What a list SAVES is a composer —
// the same one a report saves — so the pair that reads and writes one is the pair that runs here.
bool ibPropertyDynamicList::ReadNodeValue(const ibDataValue& value)
{
	return ibCompositionDescriptionMemory::ReadNode(value, GetValueAsCompositionDesc());
}

bool ibPropertyDynamicList::WriteNodeValue(ibDataValue& value) const
{
	return ibCompositionDescriptionMemory::WriteNode(value, GetValueAsCompositionDesc());
}
