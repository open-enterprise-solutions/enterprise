////////////////////////////////////////////////////////////////////////////
//	Queryable-source factory — a non-owning registry of source descriptors (queryableFactory.h)
////////////////////////////////////////////////////////////////////////////

#include "queryableFactory.h"

#include "backend/appData.h"             // ibApplicationData::GetQueryableFactory (the global fallback the per-config factory descends to)
#include "backend/query/queryable.h"     // ibBackendQueryable::GetQueryTableId (ResolveById)

//////////////////////////////////////////////////////////////////////
// ibQueryableFactory
//////////////////////////////////////////////////////////////////////

ibQueryableFactory::ibQueryableFactory(ib::AppDataCtorToken)
{
}

wxString ibQueryableFactory::Key(const wxString& ns, const wxString& name)
{
	return (ns + wxT("|") + name).Upper();   // namespace + name, case-insensitive
}

void ibQueryableFactory::Register(ibQueryableSourceDescriptor* descriptor)
{
	if (descriptor == nullptr)
		return;
	const wxString ns = descriptor->GetNamespace();
	if (ns.empty())                   // unsupported metaclass (no namespace) — not a query source
		return;
	const wxString key = Key(ns, descriptor->GetName());
	UnindexById(key);                 // whatever stood under this name before is replaced
	m_descriptors[key] = descriptor;  // non-owning
	IndexById(key, descriptor);
}

void ibQueryableFactory::Unregister(ibQueryableSourceDescriptor* descriptor)
{
	if (descriptor == nullptr)
		return;
	const wxString key = Key(descriptor->GetNamespace(), descriptor->GetName());
	auto it = m_descriptors.find(key);
	if (it != m_descriptors.end() && it->second == descriptor) {   // same pointer only
		UnindexById(key);
		m_descriptors.erase(it);
		descriptor->ReleaseCompanions();   // while what they point into is still alive — see the declaration
	}
}

void ibQueryableFactory::Clear()
{
	for (auto& entry : m_descriptors)
		if (entry.second != nullptr)
			entry.second->ReleaseCompanions();   // the same reason as in Unregister
	m_descriptors.clear();   // non-owning: just drops references
	m_byTableId.clear();
	m_tableIdOfKey.clear();
}

// The id is the one the walk reads — the descriptor's own queryable, asked with no arguments.
void ibQueryableFactory::IndexById(const wxString& key, ibQueryableSourceDescriptor* descriptor)
{
	const ibBackendQueryable* q = descriptor->CreateQueryable(nullptr, 0);
	if (q == nullptr)
		return;   // no queryable without arguments: the walk would not find it by id either
	const ibMetaID tableId = q->GetQueryTableId();
	m_byTableId[tableId][key] = descriptor;
	m_tableIdOfKey[key] = tableId;
}

void ibQueryableFactory::UnindexById(const wxString& key)
{
	const auto filed = m_tableIdOfKey.find(key);
	if (filed == m_tableIdOfKey.end())
		return;
	const auto bucket = m_byTableId.find(filed->second);
	if (bucket != m_byTableId.end()) {
		bucket->second.erase(key);
		if (bucket->second.empty())
			m_byTableId.erase(bucket);
	}
	m_tableIdOfKey.erase(filed);
}

bool ibQueryableFactory::HasNamespace(const wxString& ns) const
{
	const wxString prefix = (ns + wxT("|")).Upper();
	for (const std::pair<const wxString, ibQueryableSourceDescriptor*>& kv : m_descriptors)
		if (kv.first.StartsWith(prefix))
			return true;
	return false;
}

const ibBackendQueryable* ibQueryableFactory::Resolve(const wxString& ns, const wxString& objectName,
                                                      ibValue** paParams, long lSizeArray) const
{
	auto it = m_descriptors.find(Key(ns, objectName));
	return it != m_descriptors.end() ? it->second->CreateQueryable(paParams, lSizeArray) : nullptr;
}

ibQueryableSourceDescriptor* ibQueryableFactory::FindDescriptor(const wxString& ns, const wxString& objectName) const
{
	const auto it = m_descriptors.find(Key(ns, objectName));
	return it != m_descriptors.end() ? it->second : nullptr;
}

std::vector<ibQueryableSourceDescriptor*> ibQueryableFactory::GetDescriptors() const
{
	std::vector<ibQueryableSourceDescriptor*> result;
	result.reserve(m_descriptors.size());
	for (const std::pair<const wxString, ibQueryableSourceDescriptor*>& kv : m_descriptors)
		result.push_back(kv.second);
	return result;
}

// ⭐⭐ BY THE INDEX FIRST. This was a walk over every registered source, building each one's queryable to read its
// id — and a list asks it for every row it draws (the row's state picture), so a paint walked the registry once
// per visible row (2026-09-22). The index answers what the walk found first (the first key under that id); the
// answer is checked before it is given — the same question the walk asks, of one descriptor — and an id the index
// does not hold, or holds for a source whose id has moved since, still gets the walk, exactly as before.
ibQueryableSourceDescriptor* ibQueryableFactory::ResolveDescriptorById(ibMetaID tableId) const
{
	const auto bucket = m_byTableId.find(tableId);
	if (bucket != m_byTableId.end() && !bucket->second.empty()) {
		ibQueryableSourceDescriptor* const candidate = bucket->second.begin()->second;
		const ibBackendQueryable* q = candidate->CreateQueryable(nullptr, 0);
		if (q != nullptr && q->GetQueryTableId() == tableId)
			return candidate;
	}

	for (const std::pair<const wxString, ibQueryableSourceDescriptor*>& kv : m_descriptors) {
		const ibBackendQueryable* q = kv.second->CreateQueryable(nullptr, 0);
		if (q != nullptr && q->GetQueryTableId() == tableId)
			return kv.second;
	}
	return nullptr;
}

const ibBackendQueryable* ibQueryableFactory::ResolveById(ibMetaID tableId) const
{
	ibQueryableSourceDescriptor* descriptor = ResolveDescriptorById(tableId);
	return descriptor != nullptr ? descriptor->CreateQueryable(nullptr, 0) : nullptr;
}

//////////////////////////////////////////////////////////////////////
// ibMetaQueryableFactory — per-config: own registry FIRST, then DESCEND to the global factory on a miss.
//////////////////////////////////////////////////////////////////////

const ibBackendQueryable* ibMetaQueryableFactory::Resolve(const wxString& ns, const wxString& objectName,
                                                          ibValue** paParams, long lSizeArray) const
{
	if (const ibBackendQueryable* q = ibQueryableFactory::Resolve(ns, objectName, paParams, lSizeArray))
		return q;
	const ibQueryableFactory* global = ibApplicationData::GetQueryableFactory();
	return global != nullptr ? global->Resolve(ns, objectName, paParams, lSizeArray) : nullptr;
}

ibQueryableSourceDescriptor* ibMetaQueryableFactory::FindDescriptor(const wxString& ns, const wxString& objectName) const
{
	if (ibQueryableSourceDescriptor* own = ibQueryableFactory::FindDescriptor(ns, objectName))
		return own;
	const ibQueryableFactory* global = ibApplicationData::GetQueryableFactory();
	return global != nullptr ? global->FindDescriptor(ns, objectName) : nullptr;
}

const ibBackendQueryable* ibMetaQueryableFactory::ResolveById(ibMetaID tableId) const
{
	if (const ibBackendQueryable* q = ibQueryableFactory::ResolveById(tableId))
		return q;
	const ibQueryableFactory* global = ibApplicationData::GetQueryableFactory();
	return global != nullptr ? global->ResolveById(tableId) : nullptr;
}

ibQueryableSourceDescriptor* ibMetaQueryableFactory::ResolveDescriptorById(ibMetaID tableId) const
{
	if (ibQueryableSourceDescriptor* d = ibQueryableFactory::ResolveDescriptorById(tableId))
		return d;
	const ibQueryableFactory* global = ibApplicationData::GetQueryableFactory();
	return global != nullptr ? global->ResolveDescriptorById(tableId) : nullptr;
}

// (Registration moved to ibMetaData::RegisterSource / UnregisterSource — the metadata is the facade over its own
//  per-config factory; a metaobject registers via m_metaData->RegisterSource(&m_queryable). See metaData.cpp.)
