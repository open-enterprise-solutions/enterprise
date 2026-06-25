#ifndef __PROPERTY_DYNAMIC_SOURCE_H__
#define __PROPERTY_DYNAMIC_SOURCE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/propertyManager/property/variant/variantDynamicSource.h"

class BACKEND_API ibBackendQueryable;

// ---------------------------------------------------------------------------
// ibPropertyDynamicSource — the "Source" property of a dynamic list. The chosen
// source VARIABLE (a queryable) lives in the property's VARIANT (ibVariantDataDynamicSource),
// not on the dynamic list: the list is a pure facade (GetSourceQueryable -> GetVariable()).
// Modelled on ibPropertyType (data in a variant), but with no hard type check — the
// variant just holds the queryable. The property serializes the source itself
// (ReadNodeValue/WriteNodeValue store the source's table id). The frontend property
// (ibPGDynamicSourceProperty) draws the chooser of registered sources.
// ---------------------------------------------------------------------------
class BACKEND_API ibPropertyDynamicSource : public ibProperty {
public:

	ibPropertyDynamicSource(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, new ibVariantDataDynamicSource()) {}
	ibPropertyDynamicSource(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, new ibVariantDataDynamicSource()) {}

	// The selected source variable (queryable) — data lives in the variant; the dynamic
	// list reads it through here (facade). The setter stores it into the variant.
	const ibBackendQueryable* GetVariable() const {
		return get_cell_variant<ibVariantDataDynamicSource>()->GetQueryable();
	}
	void SetVariable(const ibBackendQueryable* queryable) {
		m_propValue = new ibVariantDataDynamicSource(queryable);
	}

	// Choose the source by its registered identity — resolves the queryable into the variant.
	void SetSource(const wxString& ns, const wxString& name);

	// get property for grid — hand the owner to the frontend chooser slot.
	virtual wxObject* GetPGProperty() const override {
		if (ms_propertyDynamicSource != nullptr)
			return ms_propertyDynamicSource(m_owner, m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	// Runtime data exchange — the source's table id as the value.
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

	// Node serialization of the chosen source (its table id, resolved back on read).
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

	// Wired at frontend load by ibPG_IMPLEMENT_PROPERTY_CALLBACK in advpropDynamicSource.cpp.
	static wxObject* (*ms_propertyDynamicSource)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&);
};

#endif // __PROPERTY_DYNAMIC_SOURCE_H__
