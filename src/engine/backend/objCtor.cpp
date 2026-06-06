#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaCollection/partial/list/objectList.h"

#include "objCtor.h"

// Phase 3: the former wxClassInfo* GetClassInfo() overrides were removed.
// These meta ctors are Category B — the runtime objects they create
// (ibValueRecordDataObject, …) override GetClassType() themselves (clsid
// derived from the bound metaobject), so the registry never needs a
// typeid/wxClassInfo key for them. Only CreateObject() remains.

//reference class
ibValue* ibCtorMetaValueTypeReference::CreateObject() const
{
	return ibValueReferenceDataObject::CreateRaw(m_metaObject);
}

//list class
ibValue* ibCtorMetaValueTypeReferenceList::CreateObject() const
{
	ibValueMetaObjectRecordDataHierarchyMutableRef* folderRef = nullptr;
	ibValueMetaObjectRecordDataEnumRef* enumRef = nullptr;

	if (m_metaObject->ConvertToValue(folderRef)) {
		return new ibValueModelTreeDataObjectFolderRef(folderRef);
	}
	else if (m_metaObject->ConvertToValue(enumRef)) {
		return new ibValueListDataObjectEnumRef(enumRef);
	}

	return new ibValueListDataObjectRef((ibValueMetaObjectRecordDataMutableRef*)m_metaObject);
}

ibValue* ibCtorMetaValueTypeRegisterList::CreateObject() const
{
	return new ibValueListRegisterObject(m_metaObject);
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
