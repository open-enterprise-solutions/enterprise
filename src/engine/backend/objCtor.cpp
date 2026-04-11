#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaCollection/partial/list/objectList.h"

#include "objCtor.h"

//reference class 
wxClassInfo* ibCtorMetaValueTypeReference::GetClassInfo() const
{
	return CLASSINFO(ibValueReferenceDataObject);
}

ibValue* ibCtorMetaValueTypeReference::CreateObject() const
{
	return ibValue::CreateAndPrepareValueRef<ibValueReferenceDataObject>(m_metaObject);
}

//list class 
wxClassInfo* ibCtorMetaValueTypeReferenceList::GetClassInfo() const
{
	ibValueMetaObjectRecordDataHierarchyMutableRef* folderRef = nullptr;
	ibValueMetaObjectRecordDataEnumRef* enumRef = nullptr;
	if (m_metaObject->ConvertToValue(folderRef)) {
		return CLASSINFO(ibValueModelTreeDataObjectFolderRef);
	}
	else if (m_metaObject->ConvertToValue(enumRef)) {
		return CLASSINFO(ibValueListDataObjectEnumRef);
	}
	return CLASSINFO(ibValueListDataObjectRef);
}

ibValue* ibCtorMetaValueTypeReferenceList::CreateObject() const
{
	ibValueMetaObjectRecordDataHierarchyMutableRef* folderRef = nullptr;
	ibValueMetaObjectRecordDataEnumRef* enumRef = nullptr;

	if (m_metaObject->ConvertToValue(folderRef)) {
		return ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(folderRef);
	}
	else if (m_metaObject->ConvertToValue(enumRef)) {
		return ibValue::CreateAndPrepareValueRef<ibValueListDataObjectEnumRef>(enumRef);
	}

	return ibValue::CreateAndPrepareValueRef<ibValueListDataObjectRef>((ibValueMetaObjectRecordDataMutableRef*)m_metaObject);
}

wxClassInfo* ibCtorMetaValueTypeRegisterList::GetClassInfo() const
{
	return CLASSINFO(ibValueListRegisterObject);
}

ibValue* ibCtorMetaValueTypeRegisterList::CreateObject() const
{
	return ibValue::CreateAndPrepareValueRef<ibValueListRegisterObject>(m_metaObject);
}

//object class
wxClassInfo* ibCtorMetaValueTypeObject::GetClassInfo() const
{
	ibValuePtr<ibValueRecordDataObject> recordDataObjectValue(
		m_metaObject->CreateRecordDataObjectValue());
	return recordDataObjectValue->GetClassInfo();
}

ibValue* ibCtorMetaValueTypeObject::CreateObject() const
{
	return m_metaObject->CreateRecordDataObjectValue();
}

//manager class
wxClassInfo* ibCtorMetaValueTypeManager::GetClassInfo() const
{
	ibValuePtr<ibValueManagerDataObject> managerDataObject(
		m_metaObject->CreateManagerDataObjectValue());
	return managerDataObject->GetClassInfo();
}

ibValue* ibCtorMetaValueTypeManager::CreateObject() const
{
	return m_metaObject->CreateManagerDataObjectValue();
}

//object record key
wxClassInfo* ibCtorMetaValueTypeRecord::GetClassInfo() const
{
	return CLASSINFO(ibValueRecordKeyObject);
}

ibValue* ibCtorMetaValueTypeRecord::CreateObject() const
{
	return m_metaObject->CreateRecordKeyObjectValue();
}

//object record manager
wxClassInfo* ibCtorMetaValueTypeRecordManager::GetClassInfo() const
{
	ibValuePtr<ibValueRecordManagerObject> recordManagerObject(
		m_metaObject->CreateRecordManagerObjectValue());
	return recordManagerObject->GetClassInfo();
}

ibValue* ibCtorMetaValueTypeRecordManager::CreateObject() const
{
	return m_metaObject->CreateRecordManagerObjectValue();
}

//object record set
wxClassInfo* ibCtorMetaValueTypeRecordSet::GetClassInfo() const
{
	ibValuePtr<ibValueRecordSetObject> recordSetObject(
		m_metaObject->CreateRecordSetObjectValue());
	return recordSetObject->GetClassInfo();
}

ibValue* ibCtorMetaValueTypeRecordSet::CreateObject() const
{
	return m_metaObject->CreateRecordSetObjectValue();
}