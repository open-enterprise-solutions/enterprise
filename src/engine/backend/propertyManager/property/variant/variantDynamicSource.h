#ifndef __DYNAMIC_SOURCE_VARIANT_H__
#define __DYNAMIC_SOURCE_VARIANT_H__

#include "backend/backend_core.h"
#include <wx/variant.h>

class BACKEND_API ibBackendQueryable;

// wxVariantData holding the chosen SOURCE — kept as the source's STABLE table id, NOT a cached
// queryable pointer. The queryable is OWNED by the metaobject's source descriptor (the factory
// vends `&m_queryable`); caching that pointer dangles the moment the source is deleted or the
// metadata image is rebuilt (use-after-free — a freed descriptor's vtable is garbage). So we store
// the id and RE-RESOLVE through the factory on demand: a live source resolves to its current
// queryable, a deleted one resolves to null — never a stale pointer. Mirrors the property's own
// serialization, which already round-trips the source as its table id. Equality compares the id.
class BACKEND_API ibVariantDataDynamicSource : public wxVariantData {
public:

	ibVariantDataDynamicSource(const ibBackendQueryable* queryable = nullptr);
	ibVariantDataDynamicSource(const ibVariantDataDynamicSource& src)
		: wxVariantData(), m_tableId(src.m_tableId) {}

	// Resolve the chosen source LIVE through the factory — null when none is picked or the source
	// was deleted (never a stale/dangling pointer).
	const ibBackendQueryable* GetQueryable() const;
	void SetQueryable(const ibBackendQueryable* queryable);

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
};

#endif // __DYNAMIC_SOURCE_VARIANT_H__
