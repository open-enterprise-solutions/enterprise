#ifndef __COMPOSITION_VARIANT_H__
#define __COMPOSITION_VARIANT_H__

#include "backend/backend_core.h"
#include "core/variant.h"
#include "core/description/compositionDescription.h"

class BACKEND_API ibPropertyObject;

// The cell that holds a COMPOSITION DESCRIPTION — the Type side's shape (ibVariantAttribute),
// and nothing more than that: a description, and the owner it belongs to.
//
// ⭐⭐ NO LIVE COMPOSITION LIVES HERE. A cell stores what a property STORES, and what a property
// stores is data — the same way the type cell holds an ibTypeDescription and the source cell holds a
// table id. Whoever needs a running composition builds one from this and keeps it where it belongs;
// a live object in the cell would be a second state beside the stored one, which is exactly what
// holding the description by reference is meant to prevent.
class BACKEND_API ibVariantComposition : public ibVariant {
public:

	/////////////////////////////////////////////////////////////////////////////////////////

	// BY REFERENCE, both ways — the base rule of the family. A caller edits in place; a description
	// is heavy and is never handed back by value.
	ibCompositionDescription& GetCompositionDesc() { return m_description; }
	const ibCompositionDescription& GetCompositionDesc() const { return m_description; }

	void SetCompositionDesc(const ibCompositionDescription& desc) { m_description = desc; }

	// WHOSE CELL THIS IS — the property object that declared the property. Asked for the one thing a
	// description cannot answer by itself: which CONFIGURATION its names mean.
	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	/////////////////////////////////////////////////////////////////////////////////////////

	explicit ibVariantComposition(const ibPropertyObject* prop,
	                                  const ibCompositionDescription& desc = ibCompositionDescription())
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_description(desc) {}
	ibVariantComposition(const ibVariantComposition& src)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(src.m_ownerProperty), m_description(src.m_description) {}

	// EQUAL WHEN THEY DESCRIBE THE SAME THING — the family's own comparison (a type compares its
	// clsid list, a source its id path). What a property is worth is what it stores.
	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantComposition* src = dynamic_cast<const ibVariantComposition*>(data.GetRef());
		return src != nullptr && src->m_description == m_description;
	}

	// The query is what a composition is FOR, so it is also what it shows.
	virtual ibString GetString() const override { return m_description.m_query; }

private:

	const ibPropertyObject*  m_ownerProperty;
	ibCompositionDescription m_description;
};

#endif // __COMPOSITION_VARIANT_H__
