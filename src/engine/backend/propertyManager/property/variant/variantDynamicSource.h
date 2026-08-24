#ifndef __DYNAMIC_SOURCE_VARIANT_H__
#define __DYNAMIC_SOURCE_VARIANT_H__

#include "backend/backend_core.h"
#include <wx/variant.h>

class BACKEND_API ibBackendQueryable;
class ibQueryableSourceDescriptor;
class ibPropertyObject;   // the OWNING property object — its GetMetaData() gives the SPECIFIC config to resolve through
class BACKEND_API ibMetaData;

// wxVariantData holding the chosen SOURCE — kept as the source's STABLE table id, NOT a cached
// queryable pointer. The queryable is OWNED by the metaobject's source descriptor (the factory
// vends `&m_queryable`); caching that pointer dangles the moment the source is deleted or the
// metadata image is rebuilt (use-after-free — a freed descriptor's vtable is garbage). So we store
// the id and RE-RESOLVE through the factory on demand: a live source resolves to its current
// queryable, a deleted one resolves to null — never a stale pointer. Mirrors the property's own
// serialization, which already round-trips the source as its table id. Equality compares the id.
class BACKEND_API ibVariantDataDynamicSource : public wxVariantData {
public:

	// `owner` = the property object this cell belongs to; at construction we resolve owner→GetMetaData() ONCE and
	// keep the SPECIFIC config (each metadata owns its own queryable set — the queryable lives in the config where
	// it was created, never in a global/active one). We keep the METADATA, not the owner pointer: a form-attribute's
	// dynamic list is TRANSIENT (re-materialised on a Type change) while the grid's display cell (a COPY) outlives
	// it — storing the owner would then dangle and crash on the next paint. The config metadata outlives the list.
	ibVariantDataDynamicSource(const ibBackendQueryable* queryable = nullptr, const ibPropertyObject* owner = nullptr);
	ibVariantDataDynamicSource(const ibVariantDataDynamicSource& src)
		: wxVariantData(), m_tableId(src.m_tableId), m_metaData(src.m_metaData) {}

	// Resolve the chosen source LIVE through the factory — null when none is picked or the source
	// was deleted (never a stale/dangling pointer).
	const ibBackendQueryable* GetQueryable() const;
	void SetQueryable(const ibBackendQueryable* queryable);

	// ⭐⭐ THE ID, KEPT AS AN ID — what READING a stored source does.
	//
	// 🛑 GOING THROUGH SetQueryable LOSES IT. Reading used to be
	// `SetQueryable(factory->ResolveById(id))`, and a source that cannot be resolved AT THAT MOMENT
	// — the metatypes are not registered yet, which is ordinary during a load — resolves to null,
	// and null means `wxNOT_FOUND`. So the right id, just read out of the file, was thrown away and
	// the list came back with no source at all: "I set the main table and it is not saved"
	// (Max, 2026-08-24).
	//
	// Nothing has to resolve here. This variant resolves LAZILY — GetQueryable / GetDescriptor ask
	// the factory every time — so keeping the number is keeping the source.
	void SetTableId(const ibMetaID& tableId) { m_tableId = tableId; }
	ibMetaID GetTableId() const { return m_tableId; }

	// Resolve the chosen source's DESCRIPTOR (holder) LIVE by the same id — the parallel path to
	// GetQueryable, so the dynamic list reaches the source's command interface. Null / never stale.
	const ibQueryableSourceDescriptor* GetDescriptor() const;

	// Representation via the (live) queryable — the source's name.
	wxString MakeString() const;

	virtual ibVariantDataDynamicSource* Clone() const { return new ibVariantDataDynamicSource(*this); }

	// Compare the chosen source by its stable id — same source means equal.
	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataDynamicSource* src = dynamic_cast<ibVariantDataDynamicSource*>(&data);
		return src != nullptr && src->m_tableId == m_tableId;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const { str << MakeString(); return true; }
#endif
	virtual bool Write(wxString& str) const { str = MakeString(); return true; }

	virtual wxString GetType() const { return wxT("ibVariantDataDynamicSource"); }

private:
	ibMetaID m_tableId = wxNOT_FOUND;   // the chosen source's stable table id (queryable re-resolved on demand)
	const ibMetaData* m_metaData = nullptr;   // the SPECIFIC config this source lives in — resolved once from the owner
};

#endif // __DYNAMIC_SOURCE_VARIANT_H__
