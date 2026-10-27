#ifndef __PROPERTY_DYNAMIC_SOURCE_H__
#define __PROPERTY_DYNAMIC_SOURCE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/propertyManager/property/variant/variantDynamicSource.h"

class BACKEND_API ibBackendQueryable;
class ibQueryableSourceDescriptor;

// ---------------------------------------------------------------------------
// ibPropertyDynamicSource — the "Source" property of a dynamic list. The chosen
// source queryable lives in the property's VARIANT (ibVariantDynamicSource),
// not on the dynamic list: the list is a pure facade (GetSourceQueryable -> GetQueryable()).
// Modelled on ibPropertyType (data in a variant), but with no hard type check — the
// variant just holds the queryable. The property serializes the source itself
// (ReadNodeValue/WriteNodeValue store the source's table id). The frontend property
// (ibPGDynamicSourceProperty) draws the chooser of registered sources.
// ---------------------------------------------------------------------------
class BACKEND_API ibPropertyDynamicSource : public ibProperty {
public:

	ibPropertyDynamicSource(ibPropertyCategory* cat, const ibString& name)
		: ibProperty(cat, name, new ibVariantDynamicSource()) {}
	ibPropertyDynamicSource(ibPropertyCategory* cat, const ibString& name, const ibString& label)
		: ibProperty(cat, name, label, new ibVariantDynamicSource()) {}
	ibPropertyDynamicSource(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString)
		: ibProperty(cat, name, label, helpString, new ibVariantDynamicSource()) {}

	// The selected source queryable — data lives in the variant; the dynamic list reads it
	// through here (facade). The setter stores it into the variant.
	const ibBackendQueryable* GetQueryable() const {
		return get_cell_variant<ibVariantDynamicSource>()->GetQueryable();
	}

	// The source's DESCRIPTOR (holder) — parallel to GetQueryable(); the dynamic list reaches the
	// command interface through it. Live-resolved by the variant, never cached.
	const ibQueryableSourceDescriptor* GetDescriptor() const {
		return get_cell_variant<ibVariantDynamicSource>()->GetDescriptor();
	}
	void SetQueryable(const ibBackendQueryable* queryable) {
		m_propValue = new ibVariantDynamicSource(queryable, m_owner);   // owner → re-resolve through ITS config factory
	}

	// ⭐ THE SOURCE AS THE ID IT IS — see the variant. Reading and writing both go through this, so a
	// source that cannot be resolved AT THIS MOMENT is still carried: the number is the source, and
	// resolving it is a question asked later, by whoever needs the object.
	//
	// 🛑 …AND IT IS RESOLVED THROUGH THE OWNER'S CONFIGURATION, the way SetQueryable's is. The id
	// used to go into the cell the property was created with — made before it had an owner, so with
	// no configuration at all: after a reopen the number was there, the list's query was there, and
	// the Source showed nothing, because the id was asked of the empty global factory (Max, 2026-10-13).
	void SetTableId(const ibMetaID& tableId) {
		ibVariantDynamicSource* source = new ibVariantDynamicSource(nullptr, m_owner);
		source->SetTableId(tableId);
		m_propValue = source;
	}
	ibMetaID GetTableId() const {
		return get_cell_variant<ibVariantDynamicSource>()->GetTableId();
	}

	// Choose the source by its registered identity — resolves the queryable into the variant.
	void SetSource(const ibString& ns, const ibString& name);

	// get property for grid — hand the owner to the frontend chooser slot.
	// Runtime data exchange — the source's table id as the value.
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

	// Node serialization of the chosen source (its table id, verbatim). Copy uses the default hook (WriteNodeValue).
	// "No source picked" is a question the property answers, not something a caller infers from a
	// written value. WriteNodeValue succeeds either way (it simply writes nothing when there is no
	// queryable), so testing its verdict never detected this — it read as "always set".
	// ⭐ EMPTY MEANS NOBODY PICKED ONE — not "it does not resolve just now". Asked of the ID, because
	// a source that is registered later is a source that WAS picked, and answering "empty" for it is
	// what made a load drop it (see ReadNodeValue).
	virtual bool IsEmptyProperty() const override { return GetTableId() == wxNOT_FOUND; }

	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	// …and the sources it may read, as the desktop's Source cell's drop-down offered them (ibPGDynamicSourceProperty):
	// every source the owner's configuration registers, by its table id (what the property stores), named
	// namespace.name. (Not GetValueList: the desktop's cell keeps its own list, by position, and its inspector would
	// rebuild on every edit to match one by table id.)
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override;
};

#endif // __PROPERTY_DYNAMIC_SOURCE_H__
