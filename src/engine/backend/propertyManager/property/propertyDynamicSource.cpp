#include "propertyDynamicSource.h"

#include "backend/appData.h"
#include "backend/metaData.h"                 // ibMetaData::GetSourceFactory — resolve the source through the owner's config
#include "backend/query/queryableFactory.h"
#include "backend/query/queryable.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/compiler/value.h"


// The factory the list resolves THROUGH — the metadata it runs ON BEHALF OF (its owner's config). Its per-config
// factory descends to the global one INTERNALLY on a miss, so a bound list resolves ITS config's source (the fix for
// a copied list picking the original's columns). Entry is ALWAYS the metadata — no appData / active-metadata grab;
// a null owner-config resolves nothing. (CONST owner: the non-const GetMetaData wxFAILs.)
static ibQueryableFactory* ibDynamicSourceFactory(const ibPropertyObject* owner)
{
	const ibMetaData* md = owner != nullptr ? owner->GetMetaData() : nullptr;
	ibQueryableFactory* factory = md != nullptr ? md->GetSourceFactory() : nullptr;
	// No owner config → knock on the global base factory (the common/plugin one). It is empty today, so this is a
	// harmless safety net, not a source of the copy bug (which is fixed by resolving through the config when present).
	return factory != nullptr ? factory : ibApplicationData::GetQueryableFactory();
}

void ibPropertyDynamicSource::SetSource(const wxString& ns, const wxString& name)
{
	if (ibQueryableFactory* factory = ibDynamicSourceFactory(m_owner))
		SetQueryable(factory->Resolve(ns, name));
}

// Value = the source's table id (a stable variable id, NOT text).
bool ibPropertyDynamicSource::SetDataValue(const ibValue& varPropVal)
{
	ibQueryableFactory* factory = ibDynamicSourceFactory(m_owner);
	if (factory == nullptr)
		return false;
	SetQueryable(factory->ResolveById((ibMetaID)varPropVal.GetInteger()));
	return GetQueryable() != nullptr;
}

bool ibPropertyDynamicSource::GetDataValue(ibValue& pvarPropVal) const
{
	const ibBackendQueryable* q = GetQueryable();
	pvarPropVal = ibValue((wxLongLong_t)(q != nullptr ? q->GetQueryTableId() : 0));
	return true;
}

// SAVE / LOAD — the source's table id VERBATIM (an Int; metadata-agnostic, stable within a config). On COPY the
// source already follows the main attribute's TYPE (list_to_clsid(metaID)), which the standard metaobject copy
// remaps to the copy's id — the list is re-materialised from that type, so its queryable points at the copy with
// no remap here. A normal reopen resolves the stored id directly. (Deep dot-walk column bindings — the "leaves" —
// are handled by the source-description path, not this scalar source id.)
bool ibPropertyDynamicSource::ReadNodeValue(const ibDataValue& value)
{
	// ⭐⭐ THE ID IS STORED AS AN ID, AND RESOLVED LATER. Reading used to resolve first —
	// `SetQueryable(factory->ResolveById(id))` — and a source that does not resolve AT THIS MOMENT
	// (the metatypes are not registered yet, which is ordinary during a load) came back null, which
	// this property stores as "no source". The number was right there in the blob and was discarded.
	//
	// The variant resolves on demand, so nothing is lost by not resolving now — and a source that
	// really has been deleted still answers null when it is next asked, which is the truth about it.
	if (value.Kind() == ibDataKind::Number)
		SetTableId((ibMetaID)value.AsInt());
	return true;
}

bool ibPropertyDynamicSource::WriteNodeValue(ibDataValue& value) const
{
	// …AND WRITTEN THE SAME WAY IT IS READ. This used to write `GetQueryable()->GetQueryTableId()`,
	// so a list saved while its source could not be resolved right then wrote NOTHING — the same
	// loss as the read half, from the other side. The id is what is stored; whether it resolves
	// today is not a fact about the file.
	const ibMetaID tableId = GetTableId();
	if (tableId == wxNOT_FOUND)
		return true;   // no source picked → nothing to write
	value = ibDataValue::Int((int)tableId);
	return true;
}
