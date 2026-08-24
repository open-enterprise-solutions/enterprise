#ifndef __PROPERTY_COMPOSITION_H__
#define __PROPERTY_COMPOSITION_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/compositionDescription.h"

// base property for "composition" — the shape ibPropertyType has over an ibTypeDescription.
//
// ⭐ A LIST AND A REPORT STAND ON THE SAME THING (Max, 2026-08-23): a dynamic list is a composer plus
// a main table, a report is the composer alone, and a list is the DEGENERATE case — it fills three
// fields of the description and leaves the rest empty. So there is one property, one variant and one
// description, exactly as there is one ibPropertyType for every kind of typed field.
class BACKEND_API ibPropertyComposition : public ibProperty {
	// ONE maker with a default — ibPropertySpreadsheet's shape over its own description.
	wxVariantData* CreateVariantData(ibPropertyObject* property,
	                                 const ibCompositionDescription& val = ibCompositionDescription()) const;
public:

	ibCompositionDescription& GetValueAsCompositionDesc() const;
	void SetValue(const ibCompositionDescription& val);

	// (NO LIVE COMPOSITION HERE. A property stores a DESCRIPTION; whoever needs a running composition
	//  builds one from it and keeps it where it belongs. A live object in the cell would be a second
	//  state beside the stored one — the one that goes stale.)

	ibPropertyComposition(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyComposition(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyComposition(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibCompositionDescription& desc)
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject(), desc)) {}

	virtual bool IsEmptyProperty() const { return !GetValueAsCompositionDesc().IsOk(); }

	//set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

	//load & save object in control
	// composite node value -> a Child (struct): the main table, the query, the settings, and
	// whatever else a composition is made of — see compositionDescription.h.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

#endif // __PROPERTY_COMPOSITION_H__
