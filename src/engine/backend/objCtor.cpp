#include "backend/metaCollection/partial/reference/reference.h"

#include "objCtor.h"

// Phase 3: the former wxClassInfo* GetClassInfo() overrides were removed.
// These meta ctors are Category B — the runtime objects they create
// (ibValueRecordDataObject, …) override GetClassType() themselves (clsid
// derived from the bound metaobject), so the registry never needs a
// typeid/wxClassInfo key for them. Only CreateObject() remains.

//reference class
ibValue* ibCtorMetaValueTypeReference::CreateObject() const
{
	// OnDemand is REQUIRED here, not preferred: reading the row would materialise attribute values,
	// one of which is itself a reference, re-entering this very function -> stack overflow.
	return ibValueReferenceDataObject::Create(m_metaObject, wxNullGuid, ibReferenceLoad::OnDemand);
}

//object class
ibValue* ibCtorMetaValueTypeObject::CreateObject() const
{
	return m_metaObject->CreateRecordDataObjectValue();
}

//manager class
ibValue* ibCtorMetaValueTypeManager::CreateObject() const
{
	return m_metaObject->CreateManagerDataObjectValue();
}

//object record key
ibValue* ibCtorMetaValueTypeRecord::CreateObject() const
{
	return m_metaObject->CreateRecordKeyObjectValue();
}

//object record manager
ibValue* ibCtorMetaValueTypeRecordManager::CreateObject() const
{
	return m_metaObject->CreateRecordManagerObjectValue();
}

//object record set
ibValue* ibCtorMetaValueTypeRecordSet::CreateObject() const
{
	return m_metaObject->CreateRecordSetObjectValue();
}
