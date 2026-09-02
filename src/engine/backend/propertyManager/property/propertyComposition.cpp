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

// A NEW CELL, WHICH IS WHAT A VARIANT IS FOR. The data is reference-counted, so whoever is looking
// at the old one goes on holding it and drops it when they close — see the note on the composer
// editor's holder (composerSettings.h), which is where a window's grip on a composition belongs.
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
	//
	// ⚠ THE OWNER IS TAKEN CONST. A const METHOD makes the pointer const, not what it points at, so
	// `m_owner->GetMetaData()` still picked the non-const overload — the one that asserts and answers
	// null (propertyObject.h). The configuration was being handed over as "none" from a line written
	// to hand it over.
	const ibPropertyObject* owner = m_owner;
	pvarPropVal = new ibValueDataComposition(GetValueAsCompositionDesc(),
	                                         owner != nullptr ? owner->GetMetaData() : nullptr);
	return true;
}

////////////////////////////////////////////////////////////////////////

bool ibPropertyComposition::ReadNodeValue(const ibDataValue& value)
{
	// ⚠ THE SAME DOOR THE GETTER ABOVE HANDS OVER. What is read back holds references and enum
	// members — a filter's right side, a parameter's value — and those types exist only in the
	// configuration's own registry: without it the value factory raises "Unknown value type '<id>'"
	// on a record saved perfectly well, the id being a metaobject's (Max, 2026-08-28).
	//
	// ⚠⚠ THROUGH A CONST OWNER. `GetMetaData` has two overloads and only the CONST one answers — the
	// other asserts and returns null (propertyObject.h) — so from a non-const method like this one
	// the door came back empty and the read failed exactly as if it had never been handed in.
	const ibPropertyObject* owner = m_owner;
	return ibCompositionDescriptionMemory::ReadNode(value, GetValueAsCompositionDesc(),
		owner != nullptr ? owner->GetMetaData() : nullptr);
}

bool ibPropertyComposition::WriteNodeValue(ibDataValue& value) const
{
	return ibCompositionDescriptionMemory::WriteNode(value, GetValueAsCompositionDesc());
}
