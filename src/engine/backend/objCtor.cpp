#include "backend/metaCollection/partial/reference/reference.h"

#include "objCtor.h"
#include "backend/metaData.h"   // ibMetaData::GetListCtorsByType — the references a barrier opens to

// Phase 3: the former wxClassInfo* GetClassInfo() overrides were removed.
// These meta ctors are Category B — the runtime objects they create
// (ibValueRecordDataObject, …) override GetClassType() themselves (clsid
// derived from the bound metaobject), so the registry never needs a
// typeid/wxClassInfo key for them. Only CreateObject() remains.

//reference class
ibValue ibCtorMetaValueTypeReference::CreateObject() const
{
	// OnDemand is REQUIRED here, not preferred: reading the row would materialise attribute values,
	// one of which is itself a reference, re-entering this very function -> stack overflow.
	return ibValueReferenceDataObject::Create(m_metaObject, ibNullGuid, ibReferenceLoad::OnDemand);
}

//object class
ibValue ibCtorMetaValueTypeObject::CreateObject() const
{
	return m_metaObject->CreateRecordDataObjectValue();
}

//manager class
ibValue ibCtorMetaValueTypeManager::CreateObject() const
{
	return m_metaObject->CreateManagerDataObjectValue();
}

//object record key
ibValue ibCtorMetaValueTypeRecord::CreateObject() const
{
	return m_metaObject->CreateRecordKeyObjectValue();
}

//object record manager
ibValue ibCtorMetaValueTypeRecordManager::CreateObject() const
{
	return m_metaObject->CreateRecordManagerObjectValue();
}

//object record set
ibValue ibCtorMetaValueTypeRecordSet::CreateObject() const
{
	return m_metaObject->CreateRecordSetObjectValue();
}

ibTypeDescription ibOpenBarriers(const ibTypeDescription& declared, const ibMetaData* metaData)
{
	if (metaData == nullptr)
		return declared;
	ibTypeDescription holds;
	for (const ibClassID& clsid : declared.GetClsidList()) {
		std::vector<ibClassID> members;
		if (IsReference(clsid) && clsid_is_any(clsid))   // a family (`AnyRef`, `CatalogRef`) — read off the kind byte
			for (const ibCtorMetaValueType* reference : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference))
				if (clsid_admits(clsid, reference->GetClassType()))
					members.push_back(reference->GetClassType());
		if (members.empty())
			holds.AppendMetaType(clsid, declared.GetTypeData());
		for (const ibClassID& member : members)
			holds.AppendMetaType(member, declared.GetTypeData());
	}
	return holds;
}
