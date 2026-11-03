#include "propertyDataComposition.h"

#include "backend/propertyManager/property/variant/variantComposition.h"
#include "backend/backend_composition.h"   // ibBackendCompositionDescriptionMemory — read against the configuration

////////////////////////////////////////////////////////////////////////

ibVariant ibPropertyDataComposition::CreateVariantData(ibPropertyObject* property, const ibCompositionDescription& desc)
{
	return ibVariant(new ibVariantComposition(property, desc));
}

////////////////////////////////////////////////////////////////////////

ibCompositionDescription& ibPropertyDataComposition::GetValueAsCompositionDesc() const {
	return get_cell_variant<ibVariantComposition>()->GetCompositionDesc();
}

void ibPropertyDataComposition::SetValue(const ibCompositionDescription& val) {
	m_propValue = CreateVariantData(m_owner, val);
}

////////////////////////////////////////////////////////////////////////

// THE SAME TWO LINES a description-backed property always has. What a REPORT saves is a composer,
// and so is what a list saves — so the pair that reads and writes one is the pair that runs here.
bool ibPropertyDataComposition::ReadNodeValue(const ibDataValue& value)
{
	// …AND THE CONFIGURATION IT IS READ AGAINST. What is read back holds references and enum members
	// — a filter's right side, a parameter's value — and those types exist only in the configuration's
	// own registry: without it the value factory raises "Unknown value type '<id>'" on a record saved
	// perfectly well, the id being a metaobject's (Max, 2026-08-28).
	//
	// ⚠⚠ THROUGH A CONST OWNER. `GetMetaData` has two overloads and only the CONST one answers — the
	// other asserts and returns null (propertyObject.h) — so from a non-const method like this one
	// the door came back empty and the read failed exactly as if it had never been handed in.
	const ibPropertyObject* owner = m_owner;
	return ibBackendCompositionDescriptionMemory::ReadNode(value, GetValueAsCompositionDesc(),
		owner != nullptr ? owner->GetMetaData() : nullptr);
}

bool ibPropertyDataComposition::WriteNodeValue(ibDataValue& value) const
{
	return ibBackendCompositionDescriptionMemory::WriteNode(value, GetValueAsCompositionDesc());
}
