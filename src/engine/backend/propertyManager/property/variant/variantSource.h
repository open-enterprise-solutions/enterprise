#ifndef __SOURCE_DATA_VARIANT_H__
#define __SOURCE_DATA_VARIANT_H__

#include "variantType.h"
#include "backend/sourceDescription.h"

class BACKEND_API ibBackendSourceColumn;   // queryColumn.h — neutral source-column the dot returns

class BACKEND_API ibVariantDataAttributeSource : public ibVariantDataAttribute {
protected:
	virtual void DoSetFromMetaId(const ibMetaID& id);
	virtual void DoRefreshTypeDesc();
public:

	ibVariantDataAttributeSource(const ibBackendTypeSourceFactory* prop, const ibMetaID& id)
		: ibVariantDataAttribute(prop), m_ownerSrcProperty(prop)
	{
		SetFromMetaDesc(id);
	}

	ibVariantDataAttributeSource(const ibBackendTypeSourceFactory* prop, const ibTypeDescription& typeDesc)
		: ibVariantDataAttribute(prop, typeDesc), m_ownerSrcProperty(prop)
	{
	}

	ibVariantDataAttributeSource(const ibVariantDataAttributeSource& srcData)
		: ibVariantDataAttribute(srcData), m_ownerSrcProperty(srcData.m_ownerSrcProperty)
	{
		RefreshTypeDesc();
	}

	virtual ibVariantDataAttributeSource* Clone() const {
		return new ibVariantDataAttributeSource(*this);
	}

	virtual wxString GetType() const {
		return wxT("ibVariantDataAttributeSource");
	}

	friend class ibVariantDataSource;

protected:
	const ibBackendTypeSourceFactory* m_ownerSrcProperty = nullptr;
};

///////////////////////////////////////////////////////////////////////////

class BACKEND_API ibVariantDataSource : public wxVariantData {
	wxString MakeString() const;
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

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibVariantDataAttributeSource* CloneSourceAttribute(const ibMetaID& id) const { return new ibVariantDataAttributeSource(m_ownerProperty, id); }
	// Pull-on-get, exactly like the Type side (ibVariantDataAttribute::GetTypeDesc self-refreshes via
	// DoRefreshTypeDesc): re-resolve the leaf type through the source explorer BEFORE cloning, so the clone
	// carries the CURRENT type (a value-table column retyped in place keeps its leaf id). Folding the refresh
	// in here means callers just clone — no separate GetSourceTypeDesc() priming step.
	ibVariantDataAttributeSource* CloneSourceAttribute() const { RefreshTypeFromSource(); return new ibVariantDataAttributeSource(*m_attributeSource); }

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void SetSourceAttribute(ibVariantDataAttributeSource* data) {
		m_attributeSource->SetFromMetaDesc(wxNOT_FOUND);
		m_attributeSource->SetFromTypeDesc(data->GetTypeDesc());
	}

	ibVariantDataAttributeSource* GetSourceAttribute() const { return m_attributeSource; }

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

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	bool IsPropAllowed() const;

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibVariantDataSource(const ibBackendTypeSourceFactory* prop, const ibMetaID& id) : wxVariantData(),
		m_attributeSource(nullptr), m_ownerProperty(prop) {

		m_attributeSource = new ibVariantDataAttributeSource(prop, id);
		if (id != wxNOT_FOUND) m_sourceDesc.SetDefaultSource(id);
	}

	ibVariantDataSource(const ibBackendTypeSourceFactory* prop, const ibGuid& id, bool fillTypeDesc = true) : wxVariantData(),
		m_attributeSource(nullptr), m_ownerProperty(prop) {

		const ibMetaID mid = GetIdByGuid(id);
		m_attributeSource = new ibVariantDataAttributeSource(prop, fillTypeDesc ? mid : wxNOT_FOUND);
		if (mid != wxNOT_FOUND) m_sourceDesc.SetDefaultSource(mid);
	}

	ibVariantDataSource(const ibBackendTypeSourceFactory* prop, const ibTypeDescription& typeDesc) : wxVariantData(),
		m_attributeSource(nullptr), m_ownerProperty(prop) {

		m_attributeSource = new ibVariantDataAttributeSource(prop, typeDesc);
	}

	ibVariantDataSource(const ibBackendTypeSourceFactory* prop, const ibSourceDescription& desc) : wxVariantData(),
		m_attributeSource(nullptr), m_ownerProperty(prop), m_sourceDesc(desc) {

		m_attributeSource = new ibVariantDataAttributeSource(prop, wxNOT_FOUND);
		RefreshTypeFromSource();   // resolve the leaf type from the explorer (not the metadata-seeded leaf id)
	}

	ibVariantDataSource(const ibVariantDataSource& srcData) : wxVariantData(),
		m_attributeSource(nullptr), m_ownerProperty(srcData.m_ownerProperty), m_sourceDesc(srcData.m_sourceDesc) {

		m_attributeSource = new ibVariantDataAttributeSource(*srcData.m_attributeSource);
	}

	virtual ~ibVariantDataSource() { m_attributeSource->DecRef(); }

	virtual ibVariantDataSource* Clone() const { return new ibVariantDataSource(*this); }

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataSource* srcData = dynamic_cast<ibVariantDataSource*>(&data);
		if (srcData != nullptr) {
			ibVariantDataAttribute* srcAttr = srcData->m_attributeSource;
			wxASSERT(srcAttr);
			return m_sourceDesc.GetPath() == srcData->m_sourceDesc.GetPath() && srcAttr->Eq(*m_attributeSource);
		}
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const { return wxT("ibVariantDataSource"); }

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

protected:

	// Refresh the type helper FROM THE SOURCE via the explorer walk (metadata-agnostic; this variant holds the
	// path, so it is correct for the control's own variant AND a picker temp/clone). Called inside the getter
	// (GetSourceTypeDesc) + on every source set — refresh-on-read, so no notification mesh is needed.
	void RefreshTypeFromSource() const;

	ibSourceDescription m_sourceDesc;          // binding address: [first hop .. leaf], metaIds
	const ibBackendTypeSourceFactory* m_ownerProperty = nullptr;
	ibVariantDataAttributeSource* m_attributeSource = nullptr;  // live type helper (derived from leaf, not serialised)
};

#endif // !__SOURCE_DATA_VARIANT_H__
