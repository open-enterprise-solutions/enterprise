#ifndef __SOURCE_DATA_VARIANT_H__
#define __SOURCE_DATA_VARIANT_H__

#include "core/variant.h"
#include "variantType.h"
#include "core/description/sourceDescription.h"

class BACKEND_API ibBackendSourceColumn;   // queryColumn.h — neutral source-column the dot returns

class BACKEND_API ibVariantAttributeSource : public ibVariantAttribute {
protected:
	virtual void DoSetFromMetaId(const ibMetaID& id);
	virtual void DoRefreshTypeDesc();
public:

	ibVariantAttributeSource(const ibBackendTypeSourceFactory* prop, const ibMetaID& id)
		: ibVariantAttribute(prop), m_ownerSrcProperty(prop)
	{
		SetFromMetaDesc(id);
	}

	ibVariantAttributeSource(const ibBackendTypeSourceFactory* prop, const ibTypeDescription& typeDesc)
		: ibVariantAttribute(prop, typeDesc), m_ownerSrcProperty(prop)
	{
	}

	ibVariantAttributeSource(const ibVariantAttributeSource& srcData)
		: ibVariantAttribute(srcData), m_ownerSrcProperty(srcData.m_ownerSrcProperty)
	{
		RefreshTypeDesc();
	}

	friend class ibVariantSource;

protected:
	const ibBackendTypeSourceFactory* m_ownerSrcProperty = nullptr;
};

///////////////////////////////////////////////////////////////////////////

class BACKEND_API ibVariantSource : public ibVariant {
public:

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// Empty when no leaf is bound OR the leaf no longer resolves (e.g. the attribute was
	// removed from the metadata) — never a live binding to a dangling metaId. Resolves
	// the leaf, matching the historical guid-based behaviour; callers rely on this to skip
	// a removed attribute instead of dereferencing a null metaobject.
	bool IsEmptySource() const;

	// True when the binding walks one or more references before the leaf (a dotted
	// path) rather than a single direct column. Cheap — just the path length.
	bool IsDotWalk() const { return m_sourceDesc.IsDotWalk(); }

	// Every hop of the binding may be shown (functional options) — the leaf and each reference before it. Unbound,
	// a whole-attribute binding or a broken path lands on no field and is not this question's: true.
	bool IsSourceAvailable() const;

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibVariantAttributeSource* CloneSourceAttribute(const ibMetaID& id) const { return new ibVariantAttributeSource(m_ownerProperty, id); }
	// Pull-on-get, exactly like the Type side (ibVariantAttribute::GetTypeDesc self-refreshes via
	// DoRefreshTypeDesc): re-resolve the leaf type through the source explorer BEFORE cloning, so the clone
	// carries the CURRENT type (a value-table column retyped in place keeps its leaf id). Folding the refresh
	// in here means callers just clone — no separate GetSourceTypeDesc() priming step.
	ibVariantAttributeSource* CloneSourceAttribute() const { RefreshTypeFromSource(); return new ibVariantAttributeSource(*m_attributeSource); }

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void SetSourceAttribute(ibVariantAttributeSource* data) {
		m_attributeSource->SetFromMetaDesc(wxNOT_FOUND);
		m_attributeSource->SetFromTypeDesc(data->GetTypeDesc());
	}

	ibVariantAttributeSource* GetSourceAttribute() const { return m_attributeSource; }

	//////////////////////////////////////////////////

	const ibBackendSourceColumn* GetSourceAttributeObject() const;

	//////////////////////////////////////////////////

	// The binding address: a single ordered metaId path (first hop .. leaf). This is
	// the variant's only stored state — what gets serialised (mirrors ibMetaDescription
	// on the meta-binding variants) and fed to the source object as a plain path.
	ibSourceDescription& GetSourceDesc() { return m_sourceDesc; }
	const ibSourceDescription& GetSourceDesc() const { return m_sourceDesc; }

	// Replace the whole path (e.g. the picker committing a chosen field), refreshing
	// the live type helper from the new leaf.
	void SetSourceDesc(const ibSourceDescription& desc, bool fillTypeDesc = true) {
		m_sourceDesc = desc;
		if (fillTypeDesc)
			RefreshTypeFromSource();
	}

	//////////////////////////////////////////////////

	void SetSource(const ibMetaID& id, bool fillTypeDesc = true);
	ibMetaID GetSource() const { return m_sourceDesc.GetLeaf(); }   // the leaf — the column read/written

	//////////////////////////////////////////////////

	void SetSourceGuid(const ibGuid& guid, bool fillTypeDesc = true);
	ibGuid GetSourceGuid() const { return GetGuidByID(m_sourceDesc.GetLeaf()); }

	//////////////////////////////////////////////////

	void SetSourceTypeDesc(const ibTypeDescription& td);
	ibTypeDescription& GetSourceTypeDesc(bool fillTypeDesc = true) const;

	//////////////////////////////////////////////////

	void ResetSource();

	//////////////////////////////////////////////////

	ibMetaID GetIdByGuid(const ibGuid& guid) const;
	ibGuid GetGuidByID(const ibMetaID& id) const;   // == the metaobject's GetCommonGuid (its copy-guid while a copy is live)

	//////////////////////////////////////////////////

	// Available source HOLDERS from the owning factory (the control). The
	// property source exposes the picker's choices through this.
	bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const {
		return m_ownerProperty != nullptr ? m_ownerProperty->GetSourceList(out) : false;
	}

	// Config metadata behind this binding — drives the metaId<->guid resolution at save/load.
	const class ibMetaData* GetMetaData() const { return m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr; }
	// …and the owning factory (the control) itself — what it may be bound to, asked of it by the property's editor.
	const ibBackendTypeSourceFactory* GetSourceFactory() const { return m_ownerProperty; }

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	bool IsPropAllowed() const;

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibVariantSource(const ibBackendTypeSourceFactory* prop, const ibMetaID& id) : ibVariant(ibValueTypes::TYPE_VALUE),
		m_ownerProperty(prop), m_attributeSource(nullptr) {

		m_attributeSource = new ibVariantAttributeSource(prop, id);
		m_attributeSource->IncrRef();   // this value's own hold on it
		if (id != wxNOT_FOUND) m_sourceDesc.SetDefaultSource(id);
	}

	ibVariantSource(const ibBackendTypeSourceFactory* prop, const ibGuid& id, bool fillTypeDesc = true) : ibVariant(ibValueTypes::TYPE_VALUE),
		m_ownerProperty(prop), m_attributeSource(nullptr) {

		const ibMetaID mid = GetIdByGuid(id);
		m_attributeSource = new ibVariantAttributeSource(prop, fillTypeDesc ? mid : wxNOT_FOUND);
		m_attributeSource->IncrRef();   // this value's own hold on it
		if (mid != wxNOT_FOUND) m_sourceDesc.SetDefaultSource(mid);
	}

	ibVariantSource(const ibBackendTypeSourceFactory* prop, const ibTypeDescription& typeDesc) : ibVariant(ibValueTypes::TYPE_VALUE),
		m_ownerProperty(prop), m_attributeSource(nullptr) {

		m_attributeSource = new ibVariantAttributeSource(prop, typeDesc);
		m_attributeSource->IncrRef();   // this value's own hold on it
	}

	ibVariantSource(const ibBackendTypeSourceFactory* prop, const ibSourceDescription& desc) : ibVariant(ibValueTypes::TYPE_VALUE),
		m_sourceDesc(desc), m_ownerProperty(prop), m_attributeSource(nullptr) {

		m_attributeSource = new ibVariantAttributeSource(prop, wxNOT_FOUND);
		m_attributeSource->IncrRef();   // this value's own hold on it
		RefreshTypeFromSource();   // resolve the leaf type from the explorer (not the metadata-seeded leaf id)
	}

	ibVariantSource(const ibVariantSource& srcData) : ibVariant(ibValueTypes::TYPE_VALUE),
		m_sourceDesc(srcData.m_sourceDesc), m_ownerProperty(srcData.m_ownerProperty), m_attributeSource(nullptr) {

		m_attributeSource = new ibVariantAttributeSource(*srcData.m_attributeSource);
		m_attributeSource->IncrRef();   // this value's own hold on it
	}

	virtual ~ibVariantSource() { m_attributeSource->DecrRef(); }

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantSource* srcData = dynamic_cast<const ibVariantSource*>(data.GetRef());
		if (srcData != nullptr) {
			ibVariantAttribute* srcAttr = srcData->m_attributeSource;
			wxASSERT(srcAttr);
			return m_sourceDesc.GetPath() == srcData->m_sourceDesc.GetPath() && srcAttr->CompareValueEQ(*m_attributeSource);
		}
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	virtual ibString GetString() const override;

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

protected:

	// Refresh the type helper FROM THE SOURCE via the explorer walk (metadata-agnostic; this variant holds the
	// path, so it is correct for the control's own variant AND a picker temp/clone). Called inside the getter
	// (GetSourceTypeDesc) + on every source set — refresh-on-read, so no notification mesh is needed.
	void RefreshTypeFromSource() const;

	ibSourceDescription m_sourceDesc;          // binding address: [first hop .. leaf], metaIds
	const ibBackendTypeSourceFactory* m_ownerProperty = nullptr;
	ibVariantAttributeSource* m_attributeSource = nullptr;  // live type helper (derived from leaf, not serialised)
};

#endif // !__SOURCE_DATA_VARIANT_H__
