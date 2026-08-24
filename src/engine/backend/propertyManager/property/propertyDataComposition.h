#ifndef __PROPERTY_DATA_COMPOSITION_H__
#define __PROPERTY_DATA_COMPOSITION_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/compositionDescription.h"   // what it stores — the SAME composer a list stores

// ---------------------------------------------------------------------------
// ibPropertyDataComposition — backend half of the "data composer settings"
// designer property. Pure action property (no stored scalar of its own): it
// surfaces a single "Settings…" action on the composition that created it.
//
// ⭐ ITS OWN TYPE, not the dynamic list's (Max, 2026-08-20). A list and a
// composition are two different things with two different settings windows —
// the list's leads with the query, the composition's with the output structure
// — and the registry matches the FRONTEND property by the BACKEND type. Sharing
// one type meant one frontend property that forked inside its click handler on
// what the owner turned out to be: a branch standing in for a type.
//
// It deliberately does NOT derive ibPropertyDynamicList: the registry matches by
// HIERARCHY (a dynamic_cast per entry), so a subclass would answer to the list's
// maker as well and the pair would be decided by registration order.
// ---------------------------------------------------------------------------
class BACKEND_API ibPropertyDataComposition : public ibProperty {
	// ONE maker with a default — the dynamic list's shape over the same description, because a list
	// and a report SAVE the same thing.
	wxVariantData* CreateVariantData(ibPropertyObject* property,
	                                 const ibCompositionDescription& val = ibCompositionDescription()) const;
public:

	// ⭐⭐ IT HOLDS A DESCRIPTION, and the action is what the inspector shows over it (2026-08-23).
	// It used to carry an empty wxVariant and nothing else: the window edited the composition and
	// the composition serialised itself, so "what a report's settings are" was answered in two
	// places. It is this property's cell now, read and written by the one pair that reads and
	// writes a composition anywhere — the very same lines the dynamic list runs.
	ibPropertyDataComposition(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	ibPropertyDataComposition(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	ibPropertyDataComposition(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	// THE DESCRIPTION THIS PROPERTY STORES — by reference, the base rule of the family.
	ibCompositionDescription& GetValueAsCompositionDesc() const;
	void SetValue(const ibCompositionDescription& val);

	// Action property — no runtime data exchange on the cell itself; the frontend property drives
	// the click that opens the window.
	virtual bool SetDataValue(const ibValue& /*varPropVal*/) override { return false; }
	virtual bool GetDataValue(ibValue& /*pvarPropVal*/) const override { return false; }

	// …AND IT SERIALISES ITSELF, in the two one-line methods every description-backed property has.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

#endif // __PROPERTY_DATA_COMPOSITION_H__
