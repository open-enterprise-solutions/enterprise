#ifndef __PROPERTY_DYNAMIC_LIST_H__
#define __PROPERTY_DYNAMIC_LIST_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/compositionDescription.h"   // what it stores — the SAME composer a report stores

// ---------------------------------------------------------------------------
// ibPropertyDynamicList — backend half of the "dynamic list settings" designer
// property. Pure action property (no stored scalar of its own): it surfaces a
// single "Settings…" action on whatever owner created it (a form attribute
// whose Type is a dynamic list). It mirrors ibPropertyForm: GetPGProperty hands
// the owner (ibPropertyObject*) to the frontend slot, and the frontend property
// (ibPGDynamicListProperty) dynamic_casts that owner to reach the dynamic list
// and open the settings form. No backend function-pointer hook lives on the
// dynamic list itself — the action is a FRONTEND property action.
// ---------------------------------------------------------------------------
// ⭐⭐ IT STORES THE SAME COMPOSER A REPORT DOES — the difference is the SETTINGS WINDOW, not the
// data (Max, 2026-08-23). A list's window leads with the query and shows a flat grouping; a report's
// window leads with the output structure. Two windows, because they are two questions — over one
// ibCompositionDescription, so nothing about what a setting IS can differ between them.
class BACKEND_API ibPropertyDynamicList : public ibProperty {
	// ONE maker with a default, the way ibPropertySpreadsheet has one over its description — a second
	// overload for "no value yet" would be the empty description spelled twice.
	wxVariantData* CreateVariantData(ibPropertyObject* property,
	                                 const ibCompositionDescription& val = ibCompositionDescription()) const;
public:

	// ⭐⭐ IT HOLDS A VARIANT — the same way ibPropertyType and ibPropertySource do — and what that
	// variant holds is an ibCompositionDescription (Max, 2026-08-23). A dynamic list IS a composer,
	// degenerately: a main table, a query and the same filter / sort / grouping. So the settings are
	// no longer "persisted by the owner": they are this property's cell, read and written by the one
	// pair that reads and writes a composition anywhere.
	ibPropertyDynamicList(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	ibPropertyDynamicList(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	ibPropertyDynamicList(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	// THE DESCRIPTION THIS PROPERTY STORES — by reference, the base rule of the family.
	ibCompositionDescription& GetValueAsCompositionDesc() const;
	void SetValue(const ibCompositionDescription& val);

	// get property for grid — pass the OWNER to the frontend slot (like ibPropertyForm).
	// Action property — no runtime data exchange and nothing to (de)serialize on
	// the property cell itself. The dynamic-list settings are persisted by the
	// OWNER (the form attribute), not by this property.
	virtual bool SetDataValue(const ibValue& /*varPropVal*/) override { return false; }
	virtual bool GetDataValue(ibValue& /*pvarPropVal*/) const override { return false; }

	// …AND IT SERIALISES ITSELF NOW, in the two one-line methods every description-backed property
	// has. What a saved list consists of is stated once, in compositionDescription.h, for a list and
	// a report alike — the difference between them is which fields are filled, not which code runs.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

};

#endif // __PROPERTY_DYNAMIC_LIST_H__
