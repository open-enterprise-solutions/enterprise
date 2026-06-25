////////////////////////////////////////////////////////////////////////////
//	Queryable-source factory — a non-owning registry of source descriptors (queryableFactory.h)
////////////////////////////////////////////////////////////////////////////

#include "queryableFactory.h"
#include "queryableHooks.h"

#include "backend/appData.h"             // ibApplicationData::GetQueryableFactory (heavy include kept in this TU)
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

std::vector<ibQueryableSourceDescriptor*> ibQueryableFactory::GetDescriptors() const
{
	std::vector<ibQueryableSourceDescriptor*> result;
	result.reserve(m_descriptors.size());
	for (const std::pair<const wxString, ibQueryableSourceDescriptor*>& kv : m_descriptors)
		result.push_back(kv.second);
	return result;
}

const ibBackendQueryable* ibQueryableFactory::ResolveById(ibMetaID tableId) const
{
	for (const std::pair<const wxString, ibQueryableSourceDescriptor*>& kv : m_descriptors) {
		const ibBackendQueryable* q = kv.second->CreateQueryable(nullptr, 0);
		if (q != nullptr && q->GetQueryTableId() == tableId)
			return q;
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////
// Light registration hooks (queryableHooks.h) — keep appData / factory out of the
// metadata TUs. The family checks onlyLoadFlag BEFORE calling register.
//////////////////////////////////////////////////////////////////////

void ibRegisterQueryableSource(ibQueryableSourceDescriptor* descriptor)
{
	if (ibQueryableFactory* factory = ibApplicationData::GetQueryableFactory())
		factory->Register(descriptor);
}

void ibUnregisterQueryableSource(ibQueryableSourceDescriptor* descriptor)
{
	if (ibQueryableFactory* factory = ibApplicationData::GetQueryableFactory())
		factory->Unregister(descriptor);
}
