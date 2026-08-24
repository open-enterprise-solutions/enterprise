#include "propertyComposition.h"

#include "backend/propertyManager/property/variant/variantComposition.h"
#include "backend/system/value/valueDataComposition.h"   // the running composition a script is handed
#include "backend/serialize/dataBuilder.h"   // ibDataNode / ibDataValue (Child)

////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyComposition::CreateVariantData(ibPropertyObject* property, const ibCompositionDescription& val) const
{
	return new ibVariantDataComposition(property, val);
}

////////////////////////////////////////////////////////////////////////

ibCompositionDescription& ibPropertyComposition::GetValueAsCompositionDesc() const {
	return get_cell_variant<ibVariantDataComposition>()->GetCompositionDesc();
}

void ibPropertyComposition::SetValue(const ibCompositionDescription& val) {
	m_propValue = CreateVariantData(m_owner, val);
}

bool ibPropertyComposition::SetDataValue(const ibValue& /*varPropVal*/)
{
	return false;   // a composition is CONFIGURED, not replaced (see ibValueDataComposition)
}

// ⭐⭐ WHAT A SCRIPT GETS IS A RUNNING COMPOSITION — MADE HERE, over the description this property
// stores, with the configuration it belongs to. The property still keeps no live object: it builds
// one for the asker and hands it over, so there is nothing here to go stale and nothing to keep in
// step with the data.
bool ibPropertyComposition::GetDataValue(ibValue& pvarPropVal) const
{
	// THE OWNER'S CONFIGURATION — a property on a metaobject belongs to the one that metaobject lives
	// in (the edited one in the designer, the copy's own on a copy). A null one is a legitimate
	// answer here and the composition knows what to do with it.
	pvarPropVal = new ibValueDataComposition(GetValueAsCompositionDesc(),
	                                         m_owner != nullptr ? m_owner->GetMetaData() : nullptr);
	return true;
}

////////////////////////////////////////////////////////////////////////

bool ibPropertyComposition::ReadNodeValue(const ibDataValue& value)
{
	return ibCompositionDescriptionMemory::ReadNode(value, GetValueAsCompositionDesc());
}

bool ibPropertyComposition::WriteNodeValue(ibDataValue& value) const
{
	return ibCompositionDescriptionMemory::WriteNode(value, GetValueAsCompositionDesc());
}
