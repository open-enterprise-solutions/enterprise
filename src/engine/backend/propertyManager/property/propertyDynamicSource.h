#ifndef __PROPERTY_DYNAMIC_SOURCE_H__
#define __PROPERTY_DYNAMIC_SOURCE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/propertyManager/property/variant/variantDynamicSource.h"

class BACKEND_API ibBackendQueryable;
class ibQueryableSourceDescriptor;

// ---------------------------------------------------------------------------
// ibPropertyDynamicSource — the "Source" property of a dynamic list. The chosen
// source queryable lives in the property's VARIANT (ibVariantDataDynamicSource),
// not on the dynamic list: the list is a pure facade (GetSourceQueryable -> GetQueryable()).
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

	// The selected source queryable — data lives in the variant; the dynamic list reads it
	// through here (facade). The setter stores it into the variant.
	const ibBackendQueryable* GetQueryable() const {
		return get_cell_variant<ibVariantDataDynamicSource>()->GetQueryable();
	}

	// The source's DESCRIPTOR (holder) — parallel to GetQueryable(); the dynamic list reaches the
	// command interface through it. Live-resolved by the variant, never cached.
	const ibQueryableSourceDescriptor* GetDescriptor() const {
		return get_cell_variant<ibVariantDataDynamicSource>()->GetDescriptor();
	}
	void SetQueryable(const ibBackendQueryable* queryable) {
		m_propValue = new ibVariantDataDynamicSource(queryable, m_owner);   // owner → re-resolve through ITS config factory
	}

	// Choose the source by its registered identity — resolves the queryable into the variant.
	void SetSource(const wxString& ns, const wxString& name);

	// get property for grid — hand the owner to the frontend chooser slot.
	// Runtime data exchange — the source's table id as the value.
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

	// Node serialization of the chosen source (its table id, verbatim). Copy uses the default hook (WriteNodeValue).
	// "No source picked" is a question the property answers, not something a caller infers from a
	// written value. WriteNodeValue succeeds either way (it simply writes nothing when there is no
	// queryable), so testing its verdict never detected this — it read as "always set".
	virtual bool IsEmptyProperty() const override { return GetQueryable() == nullptr; }

	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

};

#endif // __PROPERTY_DYNAMIC_SOURCE_H__
