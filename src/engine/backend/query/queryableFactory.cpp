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
	m_descriptors[Key(ns, descriptor->GetName())] = descriptor;   // non-owning
}

void ibQueryableFactory::Unregister(ibQueryableSourceDescriptor* descriptor)
{
	if (descriptor == nullptr)
		return;
	auto it = m_descriptors.find(Key(descriptor->GetNamespace(), descriptor->GetName()));
	if (it != m_descriptors.end() && it->second == descriptor)   // same pointer only
		m_descriptors.erase(it);
}

void ibQueryableFactory::Clear()
{
	m_descriptors.clear();   // non-owning: just drops references
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

ibQueryableSourceDescriptor* ibQueryableFactory::ResolveDescriptorById(ibMetaID tableId) const
{
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
