////////////////////////////////////////////////////////////////////////////
//	Queryable-source factory — a non-owning registry of source descriptors (queryableFactory.h)
////////////////////////////////////////////////////////////////////////////

#include "queryableFactory.h"

#include "backend/appData.h"             // ibApplicationInstance::GetQueryableFactory (the global fallback the per-application factory descends to)
#include "backend/query/queryable.h"     // ibBackendQueryable::GetQueryTableId (IndexById, ResolveDescriptorById)
#include "backend/compiler/typeCtor.h"   // ibCtorAbstractType::GetClassType — a kind's class, for a source's picture

//////////////////////////////////////////////////////////////////////
// ibQueryableSourceDescriptor
//////////////////////////////////////////////////////////////////////

ibPictureID ibQueryableSourceDescriptor::GetPicture() const
{
	const ibCtorAbstractType* const kind = ibValue::GetAvailableCtor(GetNamespace());
	return kind != nullptr ? kind->GetClassType() : 0;
}

//////////////////////////////////////////////////////////////////////
// ibQueryableFactory
//////////////////////////////////////////////////////////////////////

ibQueryableFactory::ibQueryableFactory(ib::AppDataCtorToken)
{
}

ibString ibQueryableFactory::Key(const ibString& ns, const ibString& name)
{
	return (ns + ibT("|") + name).Upper();   // namespace + name, case-insensitive
}

void ibQueryableFactory::Register(ibQueryableSourceDescriptor* descriptor)
{
	if (descriptor == nullptr)
		return;
	const ibString ns = descriptor->GetNamespace();
	if (ns.empty())                   // unsupported metaclass (no namespace) — not a query source
		return;
	const ibString key = Key(ns, descriptor->GetName());
	UnindexById(key);                 // whatever stood under this name before is replaced
	m_descriptors[key] = descriptor;  // non-owning
	IndexById(key, descriptor);
}

void ibQueryableFactory::Unregister(ibQueryableSourceDescriptor* descriptor)
{
	if (descriptor == nullptr)
		return;
	const ibString key = Key(descriptor->GetNamespace(), descriptor->GetName());
	auto it = m_descriptors.find(key);
	if (it != m_descriptors.end() && it->second == descriptor) {   // same pointer only
		UnindexById(key);
		m_descriptors.erase(it);
	}
}

void ibQueryableFactory::Clear()
{
	m_descriptors.clear();   // non-owning: just drops references
	m_byTableId.clear();
	m_tableIdOfKey.clear();
}

// The id is the one the walk reads — the descriptor's own queryable, asked with no arguments and held only to be asked.
void ibQueryableFactory::IndexById(const ibString& key, ibQueryableSourceDescriptor* descriptor)
{
	const std::shared_ptr<const ibBackendQueryable> q = descriptor->CreateQueryable(nullptr, 0);
	if (q == nullptr || q->GetQueryTableId() == 0)
		return;   // no queryable without arguments, or no table behind it: nothing is found by its id
	const ibMetaID tableId = q->GetQueryTableId();
	m_byTableId[tableId][key] = descriptor;
	m_tableIdOfKey[key] = tableId;
}

void ibQueryableFactory::UnindexById(const ibString& key)
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

bool ibQueryableFactory::HasNamespace(const ibString& ns) const
{
	const ibString prefix = (ns + ibT("|")).Upper();
	for (const std::pair<const ibString, ibQueryableSourceDescriptor*>& kv : m_descriptors)
		if (kv.first.StartsWith(prefix))
			return true;
	return false;
}

ibQueryableSourceDescriptor* ibQueryableFactory::FindDescriptor(const ibString& ns, const ibString& objectName) const
{
	const auto it = m_descriptors.find(Key(ns, objectName));
	return it != m_descriptors.end() ? it->second : nullptr;
}

std::vector<ibQueryableSourceDescriptor*> ibQueryableFactory::GetDescriptors(ibQueryableSourceKind kind) const
{
	// Every one registered here — the platform's are made only by an application's factory (ibMetaQueryableFactory).
	std::vector<ibQueryableSourceDescriptor*> result;
	if (kind == ibQueryableSourceKind::Platform)
		return result;
	result.reserve(m_descriptors.size());
	for (const std::pair<const ibString, ibQueryableSourceDescriptor*>& kv : m_descriptors)
		result.push_back(kv.second);
	return result;
}

// ⭐⭐ BY THE INDEX FIRST. This was a walk over every registered source, building each one's queryable to read its
// id — and a list asks it for every row it draws (the row's state picture), so a paint walked the registry once
// per visible row (2026-09-22). The index answers what the walk found first (the first key under that id); the
// answer is checked before it is given — the same question the walk asks, of one descriptor — and an id the index
// does not hold, or holds for a source whose id has moved since, still gets the walk, exactly as before.
// ⚠ 0 IS NO TABLE'S ID — it is what a reading with no metaobject behind it answers (a virtual table's), so it would
// find whichever of those came first, and hand a stored source a reading nobody keeps.
ibQueryableSourceDescriptor* ibQueryableFactory::ResolveDescriptorById(ibMetaID tableId) const
{
	if (tableId == 0)
		return nullptr;
	const auto bucket = m_byTableId.find(tableId);
	if (bucket != m_byTableId.end() && !bucket->second.empty()) {
		ibQueryableSourceDescriptor* const candidate = bucket->second.begin()->second;
		const std::shared_ptr<const ibBackendQueryable> q = candidate->CreateQueryable(nullptr, 0);
		if (q != nullptr && q->GetQueryTableId() == tableId)
			return candidate;
	}

	for (const std::pair<const ibString, ibQueryableSourceDescriptor*>& kv : m_descriptors) {
		const std::shared_ptr<const ibBackendQueryable> q = kv.second->CreateQueryable(nullptr, 0);
		if (q != nullptr && q->GetQueryTableId() == tableId)
			return kv.second;
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////
// ibPlatformSourceRegister — the makers every application's factory reads as it is made.
//////////////////////////////////////////////////////////////////////

// Asked through a function, not a namespace-scope vector: the registers are statics in other translation units,
// constructed in no order the language promises, and the first of them may run before such a vector exists.
static std::vector<ibPlatformSourceMaker>& PlatformSourceMakers()
{
	static std::vector<ibPlatformSourceMaker> s_makers;
	return s_makers;
}

ibPlatformSourceRegister::ibPlatformSourceRegister(ibPlatformSourceMaker maker)
{
	if (maker != nullptr)
		PlatformSourceMakers().push_back(maker);
}

//////////////////////////////////////////////////////////////////////
// ibMetaQueryableFactory — per-application: own registry FIRST, then DESCEND to the global factory on a miss.
//////////////////////////////////////////////////////////////////////

ibMetaQueryableFactory::ibMetaQueryableFactory(const ibMetaData* metaData)
{
	if (metaData == nullptr)
		return;
	for (const ibPlatformSourceMaker maker : PlatformSourceMakers())
		if (std::unique_ptr<ibQueryableSourceDescriptor> source = maker(metaData)) {
			Register(source.get());
			m_platformSources.push_back(std::move(source));
		}
}

ibQueryableSourceDescriptor* ibMetaQueryableFactory::FindDescriptor(const ibString& ns, const ibString& objectName) const
{
	if (ibQueryableSourceDescriptor* own = ibQueryableFactory::FindDescriptor(ns, objectName))
		return own;
	const ibQueryableFactory* global = ibApplicationInstance::GetQueryableFactory();
	return global != nullptr ? global->FindDescriptor(ns, objectName) : nullptr;
}

ibQueryableSourceDescriptor* ibMetaQueryableFactory::ResolveDescriptorById(ibMetaID tableId) const
{
	if (ibQueryableSourceDescriptor* d = ibQueryableFactory::ResolveDescriptorById(tableId))
		return d;
	const ibQueryableFactory* global = ibApplicationInstance::GetQueryableFactory();
	return global != nullptr ? global->ResolveDescriptorById(tableId) : nullptr;
}

// (Registration moved to ibMetaData::RegisterSource / UnregisterSource — the metadata is the facade over its own
//  per-application factory; a metaobject registers via m_metaData->RegisterSource(&m_queryable). See metaData.cpp.)
